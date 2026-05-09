// NOLINTBEGIN (magicnumber)
#include "insight/metalog/metalog_engine.hpp"

#include "hll.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <picosha2.h>

namespace insight::metalog
{

namespace
{
    constexpr std::size_t kHashLeftShift{6U};
    constexpr std::size_t kHashRightShift{2U};

    std::string format_rfc3339_utc(Timestamp timestamp)
    {
        const auto secs{std::chrono::time_point_cast<std::chrono::seconds>(timestamp)};
        const std::time_t tt{std::chrono::system_clock::to_time_t(secs)};
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::optional<LogLevel>
    dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels)
    {
        if (levels.empty())
            return std::nullopt;
        auto best_it{levels.begin()};
        for (auto it{std::next(levels.begin())}; it != levels.end(); ++it)
        {
            if (it->second > best_it->second)
                best_it = it;
        }
        return best_it->first;
    }

    std::string level_to_spec_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        case LogLevel::Unknown:
        default:
            return "INFO"; // spec doesn't define UNKNOWN
        }
    }

    [[nodiscard]] std::size_t mix(std::size_t seed, std::uint64_t value) noexcept
    {
        constexpr std::size_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
        seed ^= static_cast<std::size_t>(value) + kGoldenRatio + (seed << kHashLeftShift) +
                (seed >> kHashRightShift);
        return seed;
    }

    // Shannon entropy in bits over a (possibly partial) frequency
    // distribution: -Σ p log2 p. Computed over the bucketed templates
    // we have full counts for; tail templates we only know the sum
    // of, so we treat them collectively as a single residual bucket.
    double shannon_entropy_bits(const std::vector<std::uint64_t>& counts, std::uint64_t total)
    {
        if (total == 0)
            return 0.0;
        const double inv_total = 1.0 / static_cast<double>(total);
        double h = 0.0;
        for (auto c : counts)
        {
            if (c == 0)
                continue;
            const double p = static_cast<double>(c) * inv_total;
            h -= p * std::log2(p);
        }
        return h;
    }

} // namespace

// ── compute_template_id ────────────────────────────────────────

std::string MetaLogEngine::compute_template_id(std::string_view canonical_template)
{
    constexpr std::size_t kSha256Bytes = 32;
    std::array<unsigned char, kSha256Bytes> digest{};
    picosha2::hash256(canonical_template.begin(), canonical_template.end(), digest.begin(),
                      digest.end());

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(2 + 32);
    out.append("h:");
    for (std::size_t i = 0; i < 16; ++i)
    {
        const auto byte{static_cast<unsigned>(digest[i])};
        out.push_back(kHex[(byte >> 4) & 0xF]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

// ── Engine ─────────────────────────────────────────────────────

// HyperLogLog pimpl implementation.
// Key = content_template_id + '\x1f' + decimal(param_index).
struct MetaLogEngine::HllState
{
    using HLL = detail::HyperLogLog;

    std::unordered_map<std::string, std::vector<HLL>> sketches;
    // sketches[content_id][param_index]

    void reset()
    {
        sketches.clear();
    }

    void add(const std::string& content_id, std::size_t pi, std::string_view value)
    {
        auto& v = sketches[content_id];
        if (v.size() <= pi)
            v.resize(pi + 1);
        v[pi].add(value);
    }

    [[nodiscard]] std::uint64_t estimate(const std::string& content_id,
                                         std::size_t pi) const noexcept
    {
        const auto it = sketches.find(content_id);
        if (it == sketches.end() || pi >= it->second.size())
            return 0;
        return it->second[pi].estimate();
    }
};

MetaLogEngine::MetaLogEngine() : MetaLogEngine(MetaLogConfig{}) {}

MetaLogEngine::MetaLogEngine(MetaLogConfig config)
    : config_{std::move(config)}, hll_state_{std::make_unique<HllState>()}
{
    config_.ngram_size = std::max<std::size_t>(config_.ngram_size, 2);
    config_.ngram_size = std::min<std::size_t>(config_.ngram_size, 3);
}

MetaLogEngine::~MetaLogEngine() = default;

std::size_t MetaLogEngine::NGramKeyHash::operator()(const NGramKey& key) const noexcept
{
    std::size_t seed = key.size;
    for (std::size_t index = 0; index < key.size; ++index)
        seed = mix(seed, key.ids[index]);
    return seed;
}

void MetaLogEngine::set_source(SourceBlock source)
{
    source_ = std::move(source);
}

void MetaLogEngine::open_window(Timestamp start)
{
    window_start_ = start;
    lines_observed_ = 0;
    buckets_.clear();
    template_id_cache_.clear();
    content_template_index_.clear();
    content_templates_by_internal_id_.clear();
    recent_filled_ = 0;
    recent_.fill(0);
    ngram_counts_.clear();
    ngram_total_ = 0;
    sessions_seen_.clear();
    hll_state_->reset();
    // NOTE: prev_freq_ / prev_window_end_iso_ are NOT cleared here —
    // they are the cross-window state that feeds the stability block.
}

MetaLogEngine::TemplateLookup
MetaLogEngine::content_template_id_for(const tokenization::CanonicalEvent& event)
{
    if (auto cached{template_id_cache_.find(event.template_id)};
        cached != template_id_cache_.end() && cached->second.template_str == event.template_str)
    {
        return {.content_id = &cached->second.content_id,
                .internal_id = cached->second.internal_id};
    }

    std::string content_id = compute_template_id(event.template_str);
    auto index_it{content_template_index_.find(content_id)};
    InternalTemplateID internal_id{};
    if (index_it == content_template_index_.end())
    {
        internal_id = static_cast<InternalTemplateID>(content_templates_by_internal_id_.size());
        content_templates_by_internal_id_.push_back(content_id);
        content_template_index_.emplace(content_templates_by_internal_id_.back(), internal_id);
    }
    else
    {
        internal_id = index_it->second;
    }

    TemplateCacheEntry entry{.template_str = std::string{event.template_str},
                             .content_id = std::move(content_id),
                             .internal_id = internal_id};
    auto [iterator,
          inserted]{template_id_cache_.insert_or_assign(event.template_id, std::move(entry))};
    (void)inserted;
    return {.content_id = &iterator->second.content_id,
            .internal_id = iterator->second.internal_id};
}

void MetaLogEngine::account_ngram(const NGramKey& key)
{
    auto iterator{ngram_counts_.find(key)};
    if (iterator == ngram_counts_.end())
    {
        if (ngram_counts_.size() >= config_.max_ngram_keys)
            return; // bounded: drop new keys past the cap
        ngram_counts_.emplace(key, 1);
    }
    else
    {
        ++iterator->second;
    }
    ++ngram_total_;
}

void MetaLogEngine::ingest_event(const tokenization::CanonicalEvent& event)
{
    if (!window_start_)
        throw std::logic_error{"MetaLogEngine::ingest_event called before open_window"};

    const TemplateLookup lookup = content_template_id_for(event);

    auto [bucket_it, inserted]{buckets_.try_emplace(*lookup.content_id)};
    auto& bucket{bucket_it->second};
    if (inserted)
        bucket.template_str.assign(event.template_str.begin(), event.template_str.end());
    ++bucket.count;
    ++bucket.level_counts[event.level];
    ++lines_observed_;

    // Per-param field histogram accumulation.
    // Gated on config_.max_param_histograms == 0 (default) → single
    // predicted-not-taken branch; zero extra work on the hot path.
    if (config_.max_param_histograms > 0 && !event.params.empty())
    {
        const std::size_t n{std::min(config_.max_param_histograms, event.params.size())};
        if (bucket.param_value_counts.size() < n)
        {
            bucket.param_value_counts.resize(n);
            bucket.param_totals.resize(n, 0);
        }
        for (std::size_t pi{0}; pi < n; ++pi)
        {
            ++bucket.param_totals[pi];
            auto& vcounts{bucket.param_value_counts[pi]};
            const std::string val{event.params[pi]};
            // Track the value if there is still room, or if it is already
            // tracked (update an existing counter).
            if (vcounts.size() < config_.max_histogram_values || vcounts.contains(val))
                ++vcounts[val];
            // else: value table full; total was already incremented above.

            // HLL cardinality sketch — always add regardless of value-table cap.
            hll_state_->add(*lookup.content_id, pi, val);
        }
    }

    // Cheap session tracking: one predicted-not-taken branch when all
    // events have session_key == 0 (the historical, session-agnostic
    // tokenizer output). Any non-zero key opts in to SPEC §14.
    if (event.session_key != 0) [[unlikely]]
        sessions_seen_.insert(event.session_key);

    // n-gram update. Bigram needs >=1 prior id; trigram needs >=2.
    if (config_.ngram_size == 2 && recent_filled_ >= 1)
    {
        NGramKey key{.size = 2};
        key.ids[0] = recent_[0];
        key.ids[1] = lookup.internal_id;
        account_ngram(key);
    }
    else if (config_.ngram_size == 3 && recent_filled_ >= 2)
    {
        NGramKey key{.size = 3};
        key.ids[0] = recent_[1];
        key.ids[1] = recent_[0];
        key.ids[2] = lookup.internal_id;
        account_ngram(key);
    }

    // Shift ring: [1] = old [0]; [0] = id.
    recent_[1] = recent_[0];
    recent_[0] = lookup.internal_id;
    if (recent_filled_ < 2)
        ++recent_filled_;
}

namespace
{

    // KL(p || q) over the union of keys, with Laplace add-α smoothing
    // applied to BOTH distributions so that zero entries on either
    // side don't blow up to infinity. α = 1 over the union size is a
    // standard cheap choice; the resulting divergence is biased but
    // bounded and monotone in the underlying distributional change.
    struct DivergenceResult
    {
        double kl;
        double js;
    };

    DivergenceResult divergences(const std::unordered_map<std::string, std::uint64_t>& cur,
                                 std::uint64_t cur_total,
                                 const std::unordered_map<std::string, std::uint64_t>& prev,
                                 std::uint64_t prev_total)
    {
        std::unordered_set<std::string> keys;
        keys.reserve(cur.size() + prev.size());
        for (const auto& kv : cur)
            keys.insert(kv.first);
        for (const auto& kv : prev)
            keys.insert(kv.first);
        if (keys.empty() || cur_total == 0 || prev_total == 0)
            return {0.0, 0.0};

        const double alpha = 1.0;
        const auto k{static_cast<double>(keys.size())};
        const double cur_denom = static_cast<double>(cur_total) + (alpha * k);
        const double prev_denom = static_cast<double>(prev_total) + (alpha * k);

        double kl = 0.0;
        double js = 0.0;
        for (const auto& key : keys)
        {
            const auto cur_it{cur.find(key)};
            const auto prev_it{prev.find(key)};
            const double cn = (cur_it == cur.end() ? 0.0 : static_cast<double>(cur_it->second));
            const double pn = (prev_it == prev.end() ? 0.0 : static_cast<double>(prev_it->second));
            const double p = (cn + alpha) / cur_denom;
            const double q = (pn + alpha) / prev_denom;
            const double m = 0.5 * (p + q);
            kl += p * std::log2(p / q);
            js += 0.5 * ((p * std::log2(p / m)) + (q * std::log2(q / m)));
        }
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (kl < 0.0)
            kl = 0.0;
        js = std::clamp(js, 0.0, 1.0);
        return {.kl = kl, .js = js};
    }

    std::pair<std::uint64_t, std::uint64_t>
    new_and_vanished(const std::unordered_map<std::string, std::uint64_t>& cur,
                     const std::unordered_map<std::string, std::uint64_t>& prev)
    {
        std::uint64_t added = 0;
        std::uint64_t gone = 0;
        for (const auto& kv : cur)
            if (!prev.contains(kv.first))
                ++added;
        for (const auto& kv : prev)
            if (!cur.contains(kv.first))
                ++gone;
        return {added, gone};
    }

    // JS divergence between two per-param value-count maps.
    //
    // Uses the same Laplace-smoothed log2 convention as divergences():
    //   alpha = 1, smoothed over the union of keys.
    // Returns value in [0, 1] (bits, clamped).
    // Returns 0.0 when either total is zero.
    double histogram_js(const std::unordered_map<std::string, std::uint64_t>& prev,
                        std::uint64_t prev_total,
                        const std::unordered_map<std::string, std::uint64_t>& curr,
                        std::uint64_t curr_total)
    {
        if (prev_total == 0 || curr_total == 0)
            return 0.0;

        std::unordered_set<std::string> keys;
        keys.reserve(prev.size() + curr.size());
        for (const auto& [k, _] : prev)
            keys.insert(k);
        for (const auto& [k, _] : curr)
            keys.insert(k);
        if (keys.empty())
            return 0.0;

        const double alpha = 1.0;
        const double k = static_cast<double>(keys.size());
        const double p_denom = static_cast<double>(prev_total) + alpha * k;
        const double c_denom = static_cast<double>(curr_total) + alpha * k;

        double js = 0.0;
        for (const auto& key : keys)
        {
            const auto p_it = prev.find(key);
            const auto c_it = curr.find(key);
            const double pn = p_it == prev.end() ? 0.0 : static_cast<double>(p_it->second);
            const double cn = c_it == curr.end() ? 0.0 : static_cast<double>(c_it->second);
            const double p = (pn + alpha) / p_denom;
            const double c = (cn + alpha) / c_denom;
            const double m = 0.5 * (p + c);
            js += 0.5 * ((p * std::log2(p / m)) + (c * std::log2(c / m)));
        }
        return std::clamp(js, 0.0, 1.0);
    }

} // namespace

MetaLogDocument MetaLogEngine::close_window(Timestamp end)
{
    if (!window_start_)
        throw std::logic_error{"MetaLogEngine::close_window called before open_window"};

    MetaLogDocument doc;
    doc.metalog_version = "0.2.0";
    doc.producer.version = config_.producer_version;
    doc.source = source_;

    doc.window.start_iso = format_rfc3339_utc(*window_start_);
    doc.window.end_iso = format_rfc3339_utc(end);

    const auto delta{
        std::chrono::duration_cast<std::chrono::seconds>(end - *window_start_).count()};
    doc.window.duration_seconds = delta < 0 ? 0 : static_cast<std::uint64_t>(delta);
    doc.window.lines_observed = lines_observed_;

    // Sort buckets by count desc, template_id asc for determinism.
    std::vector<std::pair<std::string, const Bucket*>> ordered;
    ordered.reserve(buckets_.size());
    for (const auto& [tid, bucket] : buckets_)
        ordered.emplace_back(tid, &bucket);
    std::ranges::sort(ordered,
                      [](const auto& lhs, const auto& rhs)
                      {
                          if (lhs.second->count != rhs.second->count)
                              return lhs.second->count > rhs.second->count;
                          return lhs.first < rhs.first;
                      });

    const auto k{std::min(config_.top_k_size, ordered.size())};
    const auto total{static_cast<double>(lines_observed_)};

    StatsBlock& stats = doc.stats;
    stats.unique_templates = ordered.size();
    stats.top_k_size = config_.top_k_size;
    stats.top_k.reserve(k);

    for (std::size_t i = 0; i < k; ++i)
    {
        TopKEntry entry;
        entry.template_id = ordered[i].first;
        if (config_.template_emission == TemplateEmissionMode::Inline)
            entry.template_str = ordered[i].second->template_str;
        // Dedup mode populates the top-level doc.templates map below.
        // IdOnly mode emits neither.
        entry.count = ordered[i].second->count;
        entry.frequency = total > 0.0 ? static_cast<double>(entry.count) / total : 0.0;
        entry.dominant_level = dominant_level_of(ordered[i].second->level_counts);

        // Per-param field histograms — only when enabled.
        if (config_.max_param_histograms > 0)
        {
            const auto& b{*ordered[i].second};
            const auto& content_id{ordered[i].first};
            for (std::size_t pi{0}; pi < b.param_value_counts.size(); ++pi)
            {
                FieldHistogram fh;
                fh.param_index = static_cast<std::uint32_t>(pi);
                fh.value_counts = b.param_value_counts[pi];
                fh.total = b.param_totals[pi];
                // Shannon entropy over the tracked values.
                // Note: when total > sum(value_counts) (cap was hit),
                // entropy is slightly under-estimated — known limitation.
                std::vector<std::uint64_t> vcounts;
                vcounts.reserve(fh.value_counts.size());
                for (const auto& [v, c] : fh.value_counts)
                    vcounts.push_back(c);
                fh.entropy_bits = shannon_entropy_bits(vcounts, fh.total);
                // HLL approximate cardinality (SPEC §3.5).
                fh.approximate_cardinality = hll_state_->estimate(content_id, pi);
                entry.field_histograms.push_back(std::move(fh));
            }
        }

        stats.top_k.push_back(std::move(entry));
    }

    std::uint64_t tail_count = 0;
    for (std::size_t i = k; i < ordered.size(); ++i)
        tail_count += ordered[i].second->count;
    stats.tail_count = tail_count;
    stats.tail_unique = ordered.size() > k ? static_cast<std::uint64_t>(ordered.size() - k) : 0;

    // entropy_bits over the full (untruncated) template distribution.
    if (lines_observed_ > 0)
    {
        std::vector<std::uint64_t> counts;
        counts.reserve(ordered.size());
        for (const auto& kv : ordered)
            counts.push_back(kv.second->count);
        stats.entropy_bits = shannon_entropy_bits(counts, lines_observed_);
    }

    // ── behavior block ──
    if (config_.top_ngrams_size > 0 && ngram_total_ > 0)
    {
        BehaviorBlock bh;
        bh.ngram_size = config_.ngram_size;
        bh.top_ngrams_size = config_.top_ngrams_size;

        // For p(last | prefix) we need the count of occurrences of
        // each prefix as the start of an n-gram. Sum over the bucket.
        std::unordered_map<NGramKey, std::uint64_t, NGramKeyHash> prefix_totals;
        prefix_totals.reserve(ngram_counts_.size());
        const std::size_t prefix_size = config_.ngram_size - 1;
        for (const auto& [key, count] : ngram_counts_)
        {
            NGramKey prefix{.size = static_cast<std::uint8_t>(prefix_size)};
            for (std::size_t index = 0; index < prefix_size; ++index)
                prefix.ids[index] = key.ids[index];
            prefix_totals[prefix] += count;
        }

        std::vector<NGramEntry> entries;
        entries.reserve(ngram_counts_.size());
        for (const auto& [key, count] : ngram_counts_)
        {
            NGramEntry entry;
            entry.sequence.reserve(key.size);
            for (std::size_t index = 0; index < key.size; ++index)
            {
                if (key.ids[index] < content_templates_by_internal_id_.size())
                    entry.sequence.push_back(content_templates_by_internal_id_[key.ids[index]]);
            }
            entry.count = count;
            NGramKey prefix{.size = static_cast<std::uint8_t>(prefix_size)};
            for (std::size_t index = 0; index < prefix_size; ++index)
                prefix.ids[index] = key.ids[index];
            const auto prefix_it{prefix_totals.find(prefix)};
            const auto pt{prefix_it == prefix_totals.end() ? 0 : prefix_it->second};
            entry.probability = pt > 0 ? static_cast<double>(count) / static_cast<double>(pt) : 0.0;
            entries.push_back(std::move(entry));
        }
        std::ranges::sort(entries,
                          [](const NGramEntry& lhs, const NGramEntry& rhs)
                          {
                              if (lhs.count != rhs.count)
                                  return lhs.count > rhs.count;
                              return lhs.sequence < rhs.sequence;
                          });
        if (entries.size() > config_.top_ngrams_size)
            entries.resize(config_.top_ngrams_size);
        bh.top_ngrams = std::move(entries);

        // Derive bigram (transition) view from the configured n-gram
        // table: count(A→B) = sum over the trailing dimension of the
        // n-gram counts that start with (A, B). When ngram_size==2
        // this is identity; when ngram_size==3 we sum out the third
        // position. Cold path — happens only at close_window.
        std::unordered_map<std::uint64_t, std::unordered_map<std::uint64_t, std::uint64_t>>
            transitions;
        transitions.reserve(content_templates_by_internal_id_.size());
        for (const auto& [key, count] : ngram_counts_)
        {
            if (key.size < 2)
                continue;
            const auto from{key.ids[0]};
            const auto to{key.ids[1]};
            transitions[from][to] += count;
        }

        std::uint64_t edge_count = 0;
        for (const auto& [from, row] : transitions)
            edge_count += row.size();
        bh.graph_edge_count = edge_count;

        // ── branching (SPEC §4.2) ──
        if (config_.top_branching_size > 0)
        {
            std::vector<BranchingEntry> branching_rows;
            branching_rows.reserve(transitions.size());
            for (const auto& [from, row] : transitions)
            {
                if (from >= content_templates_by_internal_id_.size())
                    continue;
                BranchingEntry entry;
                entry.template_id = content_templates_by_internal_id_[from];
                entry.fanout = row.size();
                std::uint64_t total = 0;
                for (const auto& [_, c] : row)
                    total += c;
                entry.total_outgoing = total;
                if (total > 0)
                {
                    const double inv = 1.0 / static_cast<double>(total);
                    double h = 0.0;
                    for (const auto& [_, c] : row)
                    {
                        if (c == 0)
                            continue;
                        const double p = static_cast<double>(c) * inv;
                        h -= p * std::log2(p);
                    }
                    entry.entropy_bits = h;
                }
                branching_rows.push_back(std::move(entry));
            }
            std::ranges::sort(branching_rows,
                              [](const BranchingEntry& l, const BranchingEntry& r)
                              {
                                  if (l.entropy_bits != r.entropy_bits)
                                      return l.entropy_bits > r.entropy_bits;
                                  if (l.total_outgoing != r.total_outgoing)
                                      return l.total_outgoing > r.total_outgoing;
                                  return l.template_id < r.template_id;
                              });
            if (branching_rows.size() > config_.top_branching_size)
                branching_rows.resize(config_.top_branching_size);
            bh.branching = std::move(branching_rows);
        }

        // ── dominant_path (SPEC §4.1) ──
        if (config_.dominant_path_max_steps > 0 && !buckets_.empty())
        {
            // Start from the highest-count template; ties → lower internal id.
            InternalTemplateID start_id{0};
            std::uint64_t best_count{0};
            for (InternalTemplateID id = 0; id < content_templates_by_internal_id_.size(); ++id)
            {
                const auto& tid{content_templates_by_internal_id_[id]};
                auto bit{buckets_.find(tid)};
                if (bit == buckets_.end())
                    continue;
                if (bit->second.count > best_count ||
                    (bit->second.count == best_count && id < start_id))
                {
                    best_count = bit->second.count;
                    start_id = id;
                }
            }

            std::vector<std::string> path;
            std::unordered_set<InternalTemplateID> seen;
            path.reserve(config_.dominant_path_max_steps + 1U);
            seen.reserve(config_.dominant_path_max_steps + 1U);
            InternalTemplateID current = start_id;
            if (current < content_templates_by_internal_id_.size())
            {
                path.push_back(content_templates_by_internal_id_[current]);
                seen.insert(current);
                for (std::size_t step = 0; step < config_.dominant_path_max_steps; ++step)
                {
                    auto row_it{transitions.find(current)};
                    if (row_it == transitions.end() || row_it->second.empty())
                        break;
                    InternalTemplateID best_to{0};
                    std::uint64_t best_to_count{0};
                    for (const auto& [to, c] : row_it->second)
                    {
                        if (c > best_to_count || (c == best_to_count && to < best_to))
                        {
                            best_to_count = c;
                            best_to = to;
                        }
                    }
                    if (seen.contains(best_to))
                        break;
                    if (best_to >= content_templates_by_internal_id_.size())
                        break;
                    path.push_back(content_templates_by_internal_id_[best_to]);
                    seen.insert(best_to);
                    current = best_to;
                }
            }
            bh.dominant_path = std::move(path);
        }

        // ── sessions (SPEC §4.3) ──
        if (!sessions_seen_.empty())
        {
            bh.sessions_observed = sessions_seen_.size();
            bh.session_aware = false; // engine does not yet partition n-grams
        }

        doc.behavior = std::move(bh);
    }

    // ── stability block ──
    // Only emitted from the second window onwards (we need a previous
    // window's frequencies to diverge from). The producer-defined
    // stability_score is 1 - js_divergence, in [0, 1] with log2 JS.
    if (config_.emit_stability && prev_window_end_iso_ && prev_total_ > 0 && lines_observed_ > 0)
    {
        std::unordered_map<std::string, std::uint64_t> cur_freq;
        cur_freq.reserve(ordered.size());
        for (const auto& [tid, bucket] : buckets_)
            cur_freq.emplace(tid, bucket.count);

        const auto [kl, js]{divergences(cur_freq, lines_observed_, prev_freq_, prev_total_)};
        const auto [added, gone]{new_and_vanished(cur_freq, prev_freq_)};

        StabilityBlock sb;
        sb.previous_window_end_iso = *prev_window_end_iso_;
        sb.kl_divergence = kl;
        sb.js_divergence = js;
        sb.new_templates = added;
        sb.vanished_templates = gone;
        sb.stability_score = 1.0 - js;
        // NOLINTNEXTLINE(readability-use-std-min-max) — hot path: defensive clamp
        // [0,1] in the common case
        if (sb.stability_score < 0.0)
            sb.stability_score = 0.0;
        // NOLINTNEXTLINE(readability-use-std-min-max) — hot path: defensive clamp
        if (sb.stability_score > 1.0)
            sb.stability_score = 1.0;
        doc.stability = std::move(sb);
    }

    // Stash this window's frequency map for the NEXT close_window's
    // stability computation, then drop the rest of the per-window state.
    if (config_.emit_stability)
    {
        prev_freq_.clear();
        prev_freq_.reserve(buckets_.size());
        for (const auto& [tid, bucket] : buckets_)
            prev_freq_.emplace(tid, bucket.count);
        prev_total_ = lines_observed_;
        prev_window_end_iso_ = doc.window.end_iso;
    }

    // ── templates dedup map (SPEC §3.4) ──
    if (config_.template_emission == TemplateEmissionMode::Dedup)
    {
        // Emit every distinct template_id observed in the window
        // (including tail templates) so consumers can resolve any id
        // referenced by stats/behavior.
        for (const auto& [tid, bucket] : buckets_)
            doc.templates.emplace(tid, bucket.template_str);
    }

    window_start_.reset();
    lines_observed_ = 0;
    buckets_.clear();
    template_id_cache_.clear();
    content_template_index_.clear();
    content_templates_by_internal_id_.clear();
    recent_filled_ = 0;
    recent_.fill(0);
    ngram_counts_.clear();
    ngram_total_ = 0;
    sessions_seen_.clear();

    return doc;
}

// ── JSON serialiser ────────────────────────────────────────────

namespace
{

    nlohmann::json behavior_to_json(const BehaviorBlock& bh)
    {
        nlohmann::json top = nlohmann::json::array();
        for (const auto& e : bh.top_ngrams)
        {
            top.push_back({
                {"sequence", e.sequence},
                {"count", e.count},
                {"probability", e.probability},
            });
        }
        nlohmann::json out{
            {"ngram_size", bh.ngram_size},
            {"top_ngrams", std::move(top)},
            {"top_ngrams_size", bh.top_ngrams_size},
        };
        if (bh.graph_edge_count)
            out["graph_edge_count"] = *bh.graph_edge_count;
        if (!bh.dominant_path.empty())
            out["dominant_path"] = bh.dominant_path;
        if (!bh.branching.empty())
        {
            nlohmann::json br = nlohmann::json::array();
            for (const auto& b : bh.branching)
            {
                br.push_back({
                    {"template_id", b.template_id},
                    {"fanout", b.fanout},
                    {"total_outgoing", b.total_outgoing},
                    {"entropy_bits", b.entropy_bits},
                });
            }
            out["branching"] = std::move(br);
        }
        if (bh.sessions_observed)
            out["sessions_observed"] = *bh.sessions_observed;
        if (bh.session_aware)
            out["session_aware"] = bh.session_aware;
        return out;
    }

    nlohmann::json stability_to_json(const StabilityBlock& sb)
    {
        return nlohmann::json{
            {"previous_window_end", sb.previous_window_end_iso},
            {"kl_divergence", sb.kl_divergence},
            {"js_divergence", sb.js_divergence},
            {"new_templates", sb.new_templates},
            {"vanished_templates", sb.vanished_templates},
            {"stability_score", sb.stability_score},
        };
    }

} // namespace

nlohmann::json to_json(const MetaLogDocument& doc)
{
    nlohmann::json j;

    j["metalog_version"] = doc.metalog_version;

    j["producer"] = {
        {"name", doc.producer.name},
        {"version", doc.producer.version},
        {"implementation_uri", doc.producer.implementation_uri},
    };

    j["window"] = {
        {"start", doc.window.start_iso},
        {"end", doc.window.end_iso},
        {"duration_seconds", doc.window.duration_seconds},
        {"lines_observed", doc.window.lines_observed},
    };

    nlohmann::json src = nlohmann::json::object();
    if (doc.source.service)
        src["service"] = *doc.source.service;
    if (doc.source.fleet)
        src["fleet"] = *doc.source.fleet;
    if (doc.source.host_count)
        src["host_count"] = *doc.source.host_count;
    if (doc.source.host)
        src["host"] = *doc.source.host;
    if (!doc.source.tags.empty())
        src["tags"] = doc.source.tags;
    j["source"] = std::move(src);

    if (!doc.templates.empty())
        j["templates"] = doc.templates;

    nlohmann::json top_k = nlohmann::json::array();
    for (const auto& entry : doc.stats.top_k)
    {
        nlohmann::json row = {
            {"template_id", entry.template_id},
            {"count", entry.count},
            {"frequency", entry.frequency},
        };
        // SPEC §3.4: inline `template` is optional. Emit only when the
        // engine config asked for inline emission.
        if (!entry.template_str.empty())
            row["template"] = entry.template_str;
        if (entry.dominant_level)
            row["level"] = level_to_spec_string(*entry.dominant_level);
        top_k.push_back(std::move(row));
    }
    nlohmann::json stats = {
        {"unique_templates", doc.stats.unique_templates},
        {"top_k_size", doc.stats.top_k_size},
        {"tail_count", doc.stats.tail_count},
        {"tail_unique", doc.stats.tail_unique},
        {"top_k", std::move(top_k)},
    };
    if (doc.stats.entropy_bits)
        stats["entropy_bits"] = *doc.stats.entropy_bits;
    j["stats"] = std::move(stats);

    if (doc.behavior)
        j["behavior"] = behavior_to_json(*doc.behavior);
    if (doc.stability)
        j["stability"] = stability_to_json(*doc.stability);

    if (!doc.provenance.empty())
    {
        nlohmann::json prov = nlohmann::json::array();
        for (const auto& p : doc.provenance)
        {
            nlohmann::json row;
            row["window"] = {
                {"start", p.window_start_iso},
                {"end", p.window_end_iso},
            };
            nlohmann::json ps = nlohmann::json::object();
            if (p.source.service)
                ps["service"] = *p.source.service;
            if (p.source.fleet)
                ps["fleet"] = *p.source.fleet;
            if (p.source.host)
                ps["host"] = *p.source.host;
            if (p.source.host_count)
                ps["host_count"] = *p.source.host_count;
            if (!p.source.tags.empty())
                ps["tags"] = p.source.tags;
            if (!ps.empty())
                row["source"] = std::move(ps);
            row["lines_observed"] = p.lines_observed;
            if (p.document_id)
                row["document_id"] = *p.document_id;
            prov.push_back(std::move(row));
        }
        j["provenance"] = std::move(prov);
    }

    return j;
}

// ── Diff serialiser (SPEC §13) ─────────────────────────────────

namespace
{
    nlohmann::json doc_ref_to_json(const DocumentRef& r)
    {
        nlohmann::json out{
            {"window", {{"start", r.window_start_iso}, {"end", r.window_end_iso}}},
        };
        if (r.document_id)
            out["document_id"] = *r.document_id;
        return out;
    }
} // namespace

nlohmann::json to_json(const MetaLogDiff& d)
{
    nlohmann::json j;
    j["diff_version"] = d.diff_version;
    j["previous"] = doc_ref_to_json(d.previous);
    j["current"] = doc_ref_to_json(d.current);
    if (d.kl_divergence)
        j["kl_divergence"] = *d.kl_divergence;
    if (d.js_divergence)
        j["js_divergence"] = *d.js_divergence;
    if (d.stability_score)
        j["stability_score"] = *d.stability_score;
    if (!d.template_deltas.empty())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : d.template_deltas)
        {
            nlohmann::json row{
                {"template_id", t.template_id},
                {"previous_count", t.previous_count},
                {"current_count", t.current_count},
                {"delta", t.delta},
            };
            if (t.previous_frequency)
                row["previous_frequency"] = *t.previous_frequency;
            if (t.current_frequency)
                row["current_frequency"] = *t.current_frequency;
            arr.push_back(std::move(row));
        }
        j["template_deltas"] = std::move(arr);
    }
    if (!d.new_templates.empty())
        j["new_templates"] = d.new_templates;
    if (!d.vanished_templates.empty())
        j["vanished_templates"] = d.vanished_templates;
    if (!d.branching_delta.empty())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& b : d.branching_delta)
        {
            arr.push_back({
                {"template_id", b.template_id},
                {"previous_entropy_bits", b.previous_entropy_bits},
                {"current_entropy_bits", b.current_entropy_bits},
                {"delta_bits", b.delta_bits},
            });
        }
        j["branching_delta"] = std::move(arr);
    }
    if (d.ngram_delta)
    {
        nlohmann::json nd;
        nd["ngram_size"] = d.ngram_delta->ngram_size;
        if (!d.ngram_delta->new_ngrams.empty())
            nd["new_ngrams"] = d.ngram_delta->new_ngrams;
        if (!d.ngram_delta->vanished_ngrams.empty())
            nd["vanished_ngrams"] = d.ngram_delta->vanished_ngrams;
        if (!d.ngram_delta->rate_changed.empty())
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : d.ngram_delta->rate_changed)
            {
                arr.push_back({
                    {"sequence", r.sequence},
                    {"previous_probability", r.previous_probability},
                    {"current_probability", r.current_probability},
                    {"delta", r.delta},
                });
            }
            nd["rate_changed"] = std::move(arr);
        }
        j["ngram_delta"] = std::move(nd);
    }
    return j;
}

// ── compose (SPEC §12) ─────────────────────────────────────────

namespace
{
    // Compare ISO 8601 lexicographically — valid for fixed-format
    // RFC 3339 UTC strings as we emit (always Z, fixed widths).
    [[nodiscard]] const std::string& iso_min(const std::string& a, const std::string& b)
    {
        if (a.empty())
            return b;
        if (b.empty())
            return a;
        return a < b ? a : b;
    }
    [[nodiscard]] const std::string& iso_max(const std::string& a, const std::string& b)
    {
        if (a.empty())
            return b;
        if (b.empty())
            return a;
        return a > b ? a : b;
    }

    SourceBlock common_source(const SourceBlock& a, const SourceBlock& b)
    {
        if (a == b)
            return a;
        SourceBlock out;
        if (a.fleet == b.fleet)
            out.fleet = a.fleet;
        if (a.service == b.service)
            out.service = a.service;
        if (a.host == b.host)
            out.host = a.host;
        // host_count: sum if both present; otherwise leave unset.
        if (a.host_count && b.host_count)
            out.host_count = *a.host_count + *b.host_count;
        // tags: keep entries present and equal in both.
        for (const auto& [k, v] : a.tags)
        {
            auto it{b.tags.find(k)};
            if (it != b.tags.end() && it->second == v)
                out.tags.emplace(k, v);
        }
        return out;
    }

    void aggregate_top_k(std::unordered_map<std::string, std::uint64_t>& counts,
                         std::unordered_map<std::string, std::string>& templates,
                         std::unordered_map<std::string, std::optional<LogLevel>>& levels,
                         const MetaLogDocument& doc)
    {
        for (const auto& e : doc.stats.top_k)
        {
            counts[e.template_id] += e.count;
            if (!e.template_str.empty() && !templates.contains(e.template_id))
                templates.emplace(e.template_id, e.template_str);
            if (e.dominant_level && !levels.contains(e.template_id))
                levels.emplace(e.template_id, e.dominant_level);
        }
        for (const auto& [tid, tstr] : doc.templates)
            if (!templates.contains(tid))
                templates.emplace(tid, tstr);
    }
} // namespace

MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    MetaLogDocument out;
    out.metalog_version = lhs.metalog_version;
    out.producer = lhs.producer;
    out.window.start_iso = iso_min(lhs.window.start_iso, rhs.window.start_iso);
    out.window.end_iso = iso_max(lhs.window.end_iso, rhs.window.end_iso);
    out.window.lines_observed = lhs.window.lines_observed + rhs.window.lines_observed;
    // duration_seconds: real wall-time across the merged envelope.
    // We cannot reparse ISO strings here without a date library;
    // approximate by sum-of-durations when windows are disjoint, and
    // by max-of-durations when they overlap. Producers can recompute
    // from start/end if exact value matters.
    out.window.duration_seconds =
        std::max(lhs.window.duration_seconds, rhs.window.duration_seconds);
    if (lhs.window.start_iso != rhs.window.start_iso || lhs.window.end_iso != rhs.window.end_iso)
        out.window.duration_seconds = lhs.window.duration_seconds + rhs.window.duration_seconds;

    out.source = common_source(lhs.source, rhs.source);

    // Aggregate top-K counts and template strings.
    std::unordered_map<std::string, std::uint64_t> counts;
    std::unordered_map<std::string, std::string> templates;
    std::unordered_map<std::string, std::optional<LogLevel>> levels;
    aggregate_top_k(counts, templates, levels, lhs);
    aggregate_top_k(counts, templates, levels, rhs);

    out.stats.top_k_size = lhs.stats.top_k_size;

    std::vector<std::pair<std::string, std::uint64_t>> ordered(counts.begin(), counts.end());
    std::ranges::sort(ordered,
                      [](const auto& a, const auto& b)
                      {
                          if (a.second != b.second)
                              return a.second > b.second;
                          return a.first < b.first;
                      });

    const auto k{std::min(out.stats.top_k_size, ordered.size())};
    out.stats.top_k.reserve(k);
    const double total_lines = static_cast<double>(out.window.lines_observed);
    for (std::size_t i = 0; i < k; ++i)
    {
        TopKEntry e;
        e.template_id = ordered[i].first;
        if (auto t{templates.find(e.template_id)}; t != templates.end())
            e.template_str = t->second; // preserved when at least one input had it inline
        e.count = ordered[i].second;
        e.frequency = total_lines > 0.0 ? static_cast<double>(e.count) / total_lines : 0.0;
        if (auto l{levels.find(e.template_id)}; l != levels.end())
            e.dominant_level = l->second;
        out.stats.top_k.push_back(std::move(e));
    }

    out.stats.unique_templates = ordered.size();
    std::uint64_t tail_count = 0;
    for (std::size_t i = k; i < ordered.size(); ++i)
        tail_count += ordered[i].second;
    out.stats.tail_count =
        tail_count + lhs.stats.tail_count + rhs.stats.tail_count; // approximate (SPEC §12.3)
    out.stats.tail_unique = ordered.size() > k ? ordered.size() - k : 0;

    // Templates dedup map: union (matches SPEC §12).
    for (auto& [tid, tstr] : templates)
        out.templates.emplace(tid, std::move(tstr));

    // Stability dropped per SPEC §12.1.

    // Provenance: extend with the two inputs.
    out.provenance = lhs.provenance;
    out.provenance.insert(out.provenance.end(), rhs.provenance.begin(), rhs.provenance.end());
    if (out.provenance.empty()) // first-level composition: add both inputs
    {
        out.provenance.push_back({lhs.window.start_iso, lhs.window.end_iso, lhs.source,
                                  lhs.window.lines_observed, std::nullopt});
        out.provenance.push_back({rhs.window.start_iso, rhs.window.end_iso, rhs.source,
                                  rhs.window.lines_observed, std::nullopt});
    }

    // Behavior: best-effort merge of top_ngrams by summing counts on
    // identical sequences. Branching/dominant_path/graph_edge_count
    // would require recomputing from the merged transition graph,
    // which we do not have post-aggregation; we drop them rather than
    // emit stale values. Producers needing fresh behaviour on a
    // composed document SHOULD re-ingest from raw sources.
    if (lhs.behavior || rhs.behavior)
    {
        BehaviorBlock bh;
        bh.ngram_size = lhs.behavior ? lhs.behavior->ngram_size : rhs.behavior->ngram_size;
        bh.top_ngrams_size =
            lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size;
        std::map<std::vector<std::string>, std::uint64_t> seq_counts;
        std::map<std::vector<std::string>, double> seq_prob_sum;
        std::map<std::vector<std::string>, std::uint64_t> seq_prob_n;
        auto absorb = [&](const std::optional<BehaviorBlock>& b)
        {
            if (!b)
                return;
            for (const auto& e : b->top_ngrams)
            {
                seq_counts[e.sequence] += e.count;
                seq_prob_sum[e.sequence] += e.probability * static_cast<double>(e.count);
                seq_prob_n[e.sequence] += e.count;
            }
        };
        absorb(lhs.behavior);
        absorb(rhs.behavior);
        std::vector<NGramEntry> entries;
        entries.reserve(seq_counts.size());
        for (auto& [seq, c] : seq_counts)
        {
            NGramEntry e;
            e.sequence = seq;
            e.count = c;
            const auto n{seq_prob_n[seq]};
            e.probability = n > 0 ? seq_prob_sum[seq] / static_cast<double>(n) : 0.0;
            entries.push_back(std::move(e));
        }
        std::ranges::sort(entries,
                          [](const NGramEntry& a, const NGramEntry& b)
                          {
                              if (a.count != b.count)
                                  return a.count > b.count;
                              return a.sequence < b.sequence;
                          });
        if (entries.size() > bh.top_ngrams_size)
            entries.resize(bh.top_ngrams_size);
        bh.top_ngrams = std::move(entries);
        // sessions_observed: sum if both present (best-effort upper bound).
        std::uint64_t sessions = 0;
        bool any_sessions = false;
        if (lhs.behavior && lhs.behavior->sessions_observed)
        {
            sessions += *lhs.behavior->sessions_observed;
            any_sessions = true;
        }
        if (rhs.behavior && rhs.behavior->sessions_observed)
        {
            sessions += *rhs.behavior->sessions_observed;
            any_sessions = true;
        }
        if (any_sessions)
            bh.sessions_observed = sessions;
        bh.session_aware = (lhs.behavior && lhs.behavior->session_aware) ||
                           (rhs.behavior && rhs.behavior->session_aware);
        out.behavior = std::move(bh);
    }

    return out;
}

// ── diff (SPEC §13) ────────────────────────────────────────────

namespace
{
    [[nodiscard]] std::unordered_map<std::string, std::uint64_t> counts_of(const MetaLogDocument& d)
    {
        std::unordered_map<std::string, std::uint64_t> out;
        out.reserve(d.stats.top_k.size());
        for (const auto& e : d.stats.top_k)
            out.emplace(e.template_id, e.count);
        return out;
    }

    [[nodiscard]] std::unordered_map<std::string, double> freqs_of(const MetaLogDocument& d)
    {
        std::unordered_map<std::string, double> out;
        out.reserve(d.stats.top_k.size());
        for (const auto& e : d.stats.top_k)
            out.emplace(e.template_id, e.frequency);
        return out;
    }
} // namespace

MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current)
{
    MetaLogDiff out;
    out.previous.window_start_iso = previous.window.start_iso;
    out.previous.window_end_iso = previous.window.end_iso;
    out.current.window_start_iso = current.window.start_iso;
    out.current.window_end_iso = current.window.end_iso;

    const auto prev_counts = counts_of(previous);
    const auto cur_counts = counts_of(current);
    const auto prev_freqs = freqs_of(previous);
    const auto cur_freqs = freqs_of(current);

    const auto [kl, js] = divergences(cur_counts, current.window.lines_observed, prev_counts,
                                      previous.window.lines_observed);
    if (current.window.lines_observed > 0 && previous.window.lines_observed > 0)
    {
        out.kl_divergence = kl;
        out.js_divergence = js;
        out.stability_score = std::clamp(1.0 - js, 0.0, 1.0);
    }

    // template_deltas: union of template_ids
    std::unordered_set<std::string> all_ids;
    all_ids.reserve(prev_counts.size() + cur_counts.size());
    for (const auto& [k, _] : prev_counts)
        all_ids.insert(k);
    for (const auto& [k, _] : cur_counts)
        all_ids.insert(k);
    out.template_deltas.reserve(all_ids.size());
    for (const auto& id : all_ids)
    {
        auto p_it{prev_counts.find(id)};
        auto c_it{cur_counts.find(id)};
        const std::uint64_t pc = p_it == prev_counts.end() ? 0 : p_it->second;
        const std::uint64_t cc = c_it == cur_counts.end() ? 0 : c_it->second;
        TemplateDelta td;
        td.template_id = id;
        td.previous_count = pc;
        td.current_count = cc;
        td.delta = static_cast<std::int64_t>(cc) - static_cast<std::int64_t>(pc);
        if (auto pf{prev_freqs.find(id)}; pf != prev_freqs.end())
            td.previous_frequency = pf->second;
        if (auto cf{cur_freqs.find(id)}; cf != cur_freqs.end())
            td.current_frequency = cf->second;
        if (pc == 0 && cc > 0)
            out.new_templates.push_back(id);
        else if (pc > 0 && cc == 0)
            out.vanished_templates.push_back(id);
        out.template_deltas.push_back(std::move(td));
    }
    std::ranges::sort(out.template_deltas,
                      [](const TemplateDelta& a, const TemplateDelta& b)
                      {
                          if (std::abs(a.delta) != std::abs(b.delta))
                              return std::abs(a.delta) > std::abs(b.delta);
                          return a.template_id < b.template_id;
                      });
    std::ranges::sort(out.new_templates);
    std::ranges::sort(out.vanished_templates);

    // branching_delta: join on template_id. Missing branching rows mean
    // "not comparable" (e.g. composed docs), not zero entropy.
    if (previous.behavior && current.behavior && !previous.behavior->branching.empty() &&
        !current.behavior->branching.empty())
    {
        std::unordered_map<std::string, double> prev_h;
        for (const auto& b : previous.behavior->branching)
            prev_h[b.template_id] = b.entropy_bits;
        std::unordered_map<std::string, double> cur_h;
        for (const auto& b : current.behavior->branching)
            cur_h[b.template_id] = b.entropy_bits;
        std::unordered_set<std::string> ids;
        for (const auto& [k, _] : prev_h)
            ids.insert(k);
        for (const auto& [k, _] : cur_h)
            ids.insert(k);
        out.branching_delta.reserve(ids.size());
        for (const auto& id : ids)
        {
            BranchingDelta bd;
            bd.template_id = id;
            bd.previous_entropy_bits = prev_h.contains(id) ? prev_h[id] : 0.0;
            bd.current_entropy_bits = cur_h.contains(id) ? cur_h[id] : 0.0;
            bd.delta_bits = bd.current_entropy_bits - bd.previous_entropy_bits;
            out.branching_delta.push_back(std::move(bd));
        }
        std::ranges::sort(out.branching_delta,
                          [](const BranchingDelta& a, const BranchingDelta& b)
                          {
                              if (std::abs(a.delta_bits) != std::abs(b.delta_bits))
                                  return std::abs(a.delta_bits) > std::abs(b.delta_bits);
                              return a.template_id < b.template_id;
                          });
    }

    // ngram_delta: new/vanished sequences and rate-changed common ones.
    if (previous.behavior && current.behavior)
    {
        NGramDelta nd;
        nd.ngram_size = current.behavior->ngram_size;
        std::map<std::vector<std::string>, double> prev_p;
        for (const auto& e : previous.behavior->top_ngrams)
            prev_p[e.sequence] = e.probability;
        std::map<std::vector<std::string>, double> cur_p;
        for (const auto& e : current.behavior->top_ngrams)
            cur_p[e.sequence] = e.probability;
        for (const auto& [seq, _] : cur_p)
            if (!prev_p.contains(seq))
                nd.new_ngrams.push_back(seq);
        for (const auto& [seq, _] : prev_p)
            if (!cur_p.contains(seq))
                nd.vanished_ngrams.push_back(seq);
        for (const auto& [seq, cp] : cur_p)
        {
            auto pp_it{prev_p.find(seq)};
            if (pp_it == prev_p.end())
                continue;
            const double delta = cp - pp_it->second;
            if (std::abs(delta) > 0.0)
                nd.rate_changed.push_back({seq, pp_it->second, cp, delta});
        }
        std::ranges::sort(nd.rate_changed,
                          [](const NGramRateChange& a, const NGramRateChange& b)
                          {
                              if (std::abs(a.delta) != std::abs(b.delta))
                                  return std::abs(a.delta) > std::abs(b.delta);
                              return a.sequence < b.sequence;
                          });
        if (!nd.new_ngrams.empty() || !nd.vanished_ngrams.empty() || !nd.rate_changed.empty())
            out.ngram_delta = std::move(nd);
    }

    // field_histogram_deltas: per-(template_id, param_index) JS divergence.
    // Only populated when both docs carry field_histograms (max_param_histograms > 0).
    // For each template_id present in both top_k lists, and for each param_index
    // that appears in both histograms, compute JS divergence between the two
    // value_counts distributions.
    {
        // Build lookup: template_id -> TopKEntry* for previous doc.
        std::unordered_map<std::string, const TopKEntry*> prev_tke;
        for (const auto& e : previous.stats.top_k)
            if (!e.field_histograms.empty())
                prev_tke[e.template_id] = &e;

        for (const auto& curr_entry : current.stats.top_k)
        {
            if (curr_entry.field_histograms.empty())
                continue;
            auto prev_it = prev_tke.find(curr_entry.template_id);
            if (prev_it == prev_tke.end())
                continue;
            const auto& prev_entry = *prev_it->second;

            for (const auto& curr_fh : curr_entry.field_histograms)
            {
                // Find the matching previous histogram for this param_index.
                const FieldHistogram* prev_fh = nullptr;
                for (const auto& ph : prev_entry.field_histograms)
                {
                    if (ph.param_index == curr_fh.param_index)
                    {
                        prev_fh = &ph;
                        break;
                    }
                }
                if (!prev_fh)
                    continue;

                FieldHistogramDelta fhd;
                fhd.template_id = curr_entry.template_id;
                fhd.param_index = curr_fh.param_index;
                fhd.previous_entropy_bits = prev_fh->entropy_bits;
                fhd.current_entropy_bits = curr_fh.entropy_bits;
                fhd.js_divergence = histogram_js(prev_fh->value_counts, prev_fh->total,
                                                 curr_fh.value_counts, curr_fh.total);
                // Cardinality tracking: propagate HLL estimates when both sides have them.
                fhd.previous_cardinality = prev_fh->approximate_cardinality;
                fhd.current_cardinality = curr_fh.approximate_cardinality;
                if (fhd.previous_cardinality > 0 || fhd.current_cardinality > 0)
                    fhd.cardinality_delta = static_cast<std::int64_t>(fhd.current_cardinality) -
                                            static_cast<std::int64_t>(fhd.previous_cardinality);
                out.field_histogram_deltas.push_back(std::move(fhd));
            }
        }

        std::ranges::sort(out.field_histogram_deltas,
                          [](const FieldHistogramDelta& a, const FieldHistogramDelta& b)
                          {
                              if (a.js_divergence != b.js_divergence)
                                  return a.js_divergence > b.js_divergence;
                              if (a.template_id != b.template_id)
                                  return a.template_id < b.template_id;
                              return a.param_index < b.param_index;
                          });
    }

    return out;
}

} // namespace insight::metalog

// NOLINTEND
