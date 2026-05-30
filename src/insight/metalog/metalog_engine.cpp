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

#include <glaze/glaze.hpp>
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

std::optional<LogLevel> dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels)
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

// Most-common announced structural role for a template (F12 → salience). Roles
// are deterministic per template, so this is effectively "the" role.
StructuralRole dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles)
{
    StructuralRole best{StructuralRole::None};
    std::uint64_t best_count{0};
    for (const auto& [role, count] : roles)
    {
        if (count > best_count)
        {
            best_count = count;
            best = role;
        }
    }
    return best;
}

// Case-insensitive substring search; `needle` must be lowercase.
[[nodiscard]] bool contains_ci(std::string_view hay, std::string_view needle) noexcept
{
    if (needle.empty() || hay.size() < needle.size())
        return false;
    const auto lower{[](char chr) noexcept
                     { return chr >= 'A' && chr <= 'Z' ? static_cast<char>(chr - 'A' + 'a') : chr; }};
    for (std::size_t start{0}; start + needle.size() <= hay.size(); ++start)
    {
        std::size_t off{0};
        for (; off < needle.size(); ++off)
            if (lower(hay[start + off]) != needle[off])
                break;
        if (off == needle.size())
            return true;
    }
    return false;
}

// The "looks-like-failure" lexicon (F7) — a secondary severity signal for lines
// whose level/role did not already mark them (e.g. a raw `FAILED`/`Traceback`).
[[nodiscard]] bool looks_like_failure(std::string_view tmpl) noexcept
{
    return contains_ci(tmpl, "error") || contains_ci(tmpl, "fatal") ||
           contains_ci(tmpl, "exception") || contains_ci(tmpl, "panic") ||
           contains_ci(tmpl, "traceback") || contains_ci(tmpl, "failed") ||
           contains_ci(tmpl, "segmentation fault");
}

// A structural branch must recur to be trusted: a transition observed ONCE is
// indistinguishable from a one-off / window-boundary artifact (e.g. a novel
// template appended as the last event of a window), and -log2(p) from a single
// sample is unreliable. Requiring ≥2 observations means "this off-path branch
// recurred — it is a real alternate path, not noise."
constexpr std::uint64_t kMinSurpriseEdgeObservations{2};

// Structural-surprise band (0..100) for an edge probability p = c/t, where the
// edge is the MOST-LIKELY incoming transition into a template (so a template is
// surprising only when even its easiest way in is rare). Integer thresholds on
// c·K vs t — no float (I5). c==0 means no incoming edge (a root / unreachable
// node): expected, not surprising. The bands sit alongside the severity ladder
// (Warn 30 … Error 80 … Fatal 100) so a rare off-path transition is a peer
// severity signal, not an afterthought.
[[nodiscard]] std::uint32_t surprise_band(std::uint64_t edge_count,
                                          std::uint64_t source_outgoing) noexcept
{
    if (edge_count < kMinSurpriseEdgeObservations || source_outgoing == 0U)
        return 0U;
    if (edge_count * 50U < source_outgoing)
        return 90U; // p < 2%   — strongly off-path
    if (edge_count * 20U < source_outgoing)
        return 75U; // p < 5%
    if (edge_count * 10U < source_outgoing)
        return 50U; // p < 10%
    if (edge_count * 5U < source_outgoing)
        return 25U; // p < 20%
    return 0U;       // common transition — on the expected flow
}

// Self-novelty band (0..100): how late a template first appeared within the
// window, from its first-seen ordinal over the window's line count. A template
// present from the start (first_seen ≈ 0) scores 0; one that EMERGED late scores
// high. Self-relative (I3) and re-derivable from provenance — NOT a baseline diff.
// Same recurrence floor as structural_surprise: a single late event is a
// window-boundary artifact, not an emergence, so require count >= 2. Integer-only
// (cross-multiply, I5). Capped at 60 — softer than severity/structure, since
// late-emergence is suggestive, not intrinsically severe.
[[nodiscard]] std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                                         std::uint64_t count) noexcept
{
    if (count < kMinSurpriseEdgeObservations || lines == 0U)
        return 0U;
    if (first_seen_index * 10U > lines * 9U)
        return 60U; // first seen in the last 10% of the window
    if (first_seen_index * 4U > lines * 3U)
        return 40U; // last 25%
    if (first_seen_index * 2U > lines)
        return 20U; // last 50%
    return 0U;       // present from the first half — not an emergence
}

// Deterministic, quantized salience (Salience epic §5.1: (severity ⊕
// structural_surprise ⊕ novelty) ⊗ rarity). Integer math only — no float in this
// retention-content path (I5). Returns 0 for a non-salient template (so rare-benign
// noise never enters the reservoir).
[[nodiscard]] std::uint32_t salience_score(std::optional<LogLevel> level, StructuralRole role,
                                           std::string_view tmpl, std::uint64_t count,
                                           std::uint64_t lines, std::uint32_t structural_surprise,
                                           std::uint32_t novelty) noexcept
{
    // severity 0..100, multi-signal max (robust to any single signal missing).
    std::uint32_t severity{0};
    if (role == StructuralRole::Terminator)
        severity = std::max<std::uint32_t>(severity, 90U);
    if (level)
    {
        switch (*level)
        {
        case LogLevel::Fatal:
            severity = std::max<std::uint32_t>(severity, 100U);
            break;
        case LogLevel::Error:
            severity = std::max<std::uint32_t>(severity, 80U);
            break;
        case LogLevel::Warn:
            severity = std::max<std::uint32_t>(severity, 30U);
            break;
        default:
            break;
        }
    }
    if (looks_like_failure(tmpl))
        severity = std::max<std::uint32_t>(severity, 70U);
    // structural_surprise and novelty are peer severity axes: a benign Info line is
    // salient if it is reached only via a rare off-path transition (STRUCTURE) or if
    // it just EMERGED late in the window (TIME), even when its level/lexicon
    // severity is 0. Soft max — robust to any single axis being absent.
    severity = std::max({severity, structural_surprise, novelty});
    if (severity == 0U)
        return 0U; // not salient — rarity must never gate a benign template in (SPEC §3.7.2)

    // rarity modulation (a modulator, never a gate): rare → amplify, frequent →
    // damp toward baseline. Integer thresholds on count·N vs lines (no float).
    std::uint32_t rarity{100U};
    if (lines > 0)
    {
        if (count * 1000U < lines)
            rarity = 100U; // < 0.1%  — rare
        else if (count * 100U < lines)
            rarity = 90U; // < 1%
        else if (count * 10U < lines)
            rarity = 60U; // < 10%
        else
            rarity = 30U; // frequent — likely known/baseline
    }
    return severity * rarity; // 0..10000
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

    // A Drain cluster whose template EVOLVED (its first literal occurrence later
    // gained a wildcard) changes content_id mid-window. Re-attribute the prior
    // occurrences, bucketed under the old literal template, to the new template so
    // the cluster stays ONE entry. Otherwise the literal first occurrence is left
    // as a count-1 singleton that a diff mis-reads as a vanished/new line — and for
    // an error/warn template it surfaces despite the low count via severity
    // promotion (the spurious "recovered"/"appeared" rows on identical errors).
    //
    // Guard on a real cluster id: canon assigns each Drain cluster a unique id >= 1,
    // so "same id, changed template" is a genuine evolution. Id 0 is the reserved
    // empty-content / unset sentinel and may front many unrelated templates (e.g.
    // synthetic events), so it must NOT trigger a merge.
    if (auto evolved{template_id_cache_.find(event.template_id)};
        event.template_id != 0 && evolved != template_id_cache_.end() &&
        evolved->second.content_id != content_id)
        migrate_bucket(evolved->second.content_id, content_id, event.template_str);

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

void MetaLogEngine::migrate_bucket(const std::string& from_content_id,
                                   const std::string& to_content_id,
                                   std::string_view new_template_str)
{
    auto from_it{buckets_.find(from_content_id)};
    if (from_it == buckets_.end())
        return; // nothing accumulated under the old template yet

    Bucket moved{std::move(from_it->second)};
    buckets_.erase(from_it);

    Bucket& dst{buckets_[to_content_id]};
    const bool dst_existed{dst.count > 0};
    dst.template_str.assign(new_template_str.begin(), new_template_str.end());
    dst.count += moved.count;
    // Earliest occurrence wins: an evolved cluster keeps its true debut ordinal so
    // novelty doesn't read it as "newly emerged" just because its content_id changed.
    dst.first_seen_index =
        dst_existed ? std::min(dst.first_seen_index, moved.first_seen_index) : moved.first_seen_index;
    for (const auto& [level, level_count] : moved.level_counts)
        dst.level_counts[level] += level_count;
    for (const auto& [role, role_count] : moved.role_counts)
        dst.role_counts[role] += role_count;

    // Param histograms (only populated when config_.max_param_histograms > 0).
    if (!moved.param_value_counts.empty())
    {
        if (dst.param_value_counts.size() < moved.param_value_counts.size())
        {
            dst.param_value_counts.resize(moved.param_value_counts.size());
            dst.param_totals.resize(moved.param_value_counts.size(), 0);
        }
        for (std::size_t pi{0}; pi < moved.param_value_counts.size(); ++pi)
        {
            dst.param_totals[pi] += moved.param_totals[pi];
            for (const auto& [value, value_count] : moved.param_value_counts[pi])
                dst.param_value_counts[pi][value] += value_count;
        }
    }
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
    {
        bucket.template_str.assign(event.template_str.begin(), event.template_str.end());
        bucket.first_seen_index = lines_observed_; // ordinal of this first occurrence
    }
    ++bucket.count;
    ++bucket.level_counts[event.level];
    ++bucket.role_counts[event.structural_role]; // F12 announced role → salience
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
    doc.metalog_version = "0.5.0";
    doc.producer.version = config_.producer_version;
    doc.source = source_;

    doc.window.start_iso = format_rfc3339_utc(*window_start_);
    doc.window.end_iso = format_rfc3339_utc(end);

    const auto delta{
        std::chrono::duration_cast<std::chrono::seconds>(end - *window_start_).count()};
    doc.window.duration_seconds = delta < 0 ? 0 : static_cast<std::uint64_t>(delta);
    doc.window.lines_observed = lines_observed_;

    // §2.4 processing identifiers: opaque names of the contract the document was
    // produced under. Stamped from config; gate compose()/diff comparability.
    doc.canonicalization_version = config_.canonicalization_version;
    doc.retention_profile = config_.retention_profile;

    // §15 re-derivation coordinate: when a source_ref is configured, stamp the
    // window's EVENT-TIME bounds as integer ticks (no float; bit-identical across
    // replays since the bounds come from the deterministic event timestamps — I5,
    // §15.3). Descriptive metadata only — it is never read by any compute below.
    if (config_.source_ref)
    {
        // §15.2 RAW coordinate: source_ref + bounds present, children absent.
        ReDerivationCoordinate coord;
        coord.source_ref = *config_.source_ref;
        coord.bounds =
            EventTimeBounds{static_cast<std::uint64_t>(window_start_->time_since_epoch().count()),
                            static_cast<std::uint64_t>(end.time_since_epoch().count())};
        coord.canonicalization_version = config_.canonicalization_version;
        doc.coordinate = std::move(coord);
    }

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

    // ── Transition graph + per-template structural surprise (2d, epic §5.1) ──
    // Built once here (cold path) from the accumulated n-grams, BEFORE reservoir
    // selection so structural_surprise can feed salience, then reused by the
    // behavior block below. A template's structural_surprise is the surprise of
    // its MOST-LIKELY incoming transition: a node reachable only via a rare edge
    // off the dominant path scores high even when its level/lexicon severity is 0
    // (the benign Info "took alternate cache path"). Integer-only (I5); the band
    // depends solely on the winning edge's probability, so unordered_map iteration
    // order cannot perturb it (equal-probability edges yield the same band).
    std::unordered_map<InternalTemplateID, std::unordered_map<InternalTemplateID, std::uint64_t>>
        transitions;
    std::vector<std::uint32_t> incoming_surprise; // indexed by internal id; 0 = on-path/root
    const bool need_graph{(config_.reservoir_size > 0 || config_.top_ngrams_size > 0) &&
                          ngram_total_ > 0};
    if (need_graph)
    {
        const auto node_count{content_templates_by_internal_id_.size()};
        transitions.reserve(node_count);
        for (const auto& [key, count] : ngram_counts_)
        {
            if (key.size < 2)
                continue;
            transitions[key.ids[0]][key.ids[1]] += count;
        }
        // Per `to`, track the highest-probability incoming edge as the ratio
        // best_c/best_t; compare ratios by cross-multiply (exact integer math).
        incoming_surprise.assign(node_count, 0U);
        std::vector<std::uint64_t> best_c(node_count, 0);
        std::vector<std::uint64_t> best_t(node_count, 1);
        for (const auto& [from, row] : transitions)
        {
            std::uint64_t outgoing{0};
            for (const auto& [to, count] : row)
                outgoing += count;
            if (outgoing == 0U)
                continue;
            for (const auto& [to, count] : row)
            {
                if (to >= node_count)
                    continue;
                if (count * best_t[to] > best_c[to] * outgoing)
                {
                    best_c[to] = count;
                    best_t[to] = outgoing;
                }
            }
        }
        for (std::size_t id = 0; id < node_count; ++id)
            incoming_surprise[id] = surprise_band(best_c[id], best_t[id]);
    }
    // Structural surprise for a bucket's content_id (0 when the graph is absent or
    // the template has no rare incoming edge).
    const auto surprise_of{[&](const std::string& content_id) noexcept -> std::uint32_t
                           {
                               if (incoming_surprise.empty())
                                   return 0U;
                               const auto it{content_template_index_.find(content_id)};
                               if (it == content_template_index_.end() ||
                                   it->second >= incoming_surprise.size())
                                   return 0U;
                               return incoming_surprise[it->second];
                           }};

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

    // ── Tier 2: Salience Reservoir (F1) ──
    // From the below-top_k templates, retain the most SALIENT (not the most
    // frequent) — this is where a rare-but-severe event (a lone fatal) survives
    // instead of collapsing into the tail. Disjoint from top_k by construction
    // (candidates are ordered[k..]); admitted templates are excluded from the
    // tail residual below so they are not double-counted. Deterministic: integer
    // salience, ranked desc with a template_id tie-break.
    std::unordered_set<std::string> reserved; // content_ids promoted to the reservoir
    if (config_.reservoir_size > 0 && ordered.size() > k)
    {
        struct Candidate
        {
            std::size_t index;
            std::uint32_t salience;
            std::uint32_t structural_surprise;
            std::uint32_t novelty;
        };
        std::vector<Candidate> candidates;
        for (std::size_t i = k; i < ordered.size(); ++i)
        {
            const Bucket& bucket{*ordered[i].second};
            const auto surprise{surprise_of(ordered[i].first)};
            const auto novelty{novelty_band(bucket.first_seen_index, lines_observed_, bucket.count)};
            const auto sal{salience_score(dominant_level_of(bucket.level_counts),
                                          dominant_role_of(bucket.role_counts), bucket.template_str,
                                          bucket.count, lines_observed_, surprise, novelty)};
            if (sal > 0U)
                candidates.push_back(Candidate{.index = i,
                                               .salience = sal,
                                               .structural_surprise = surprise,
                                               .novelty = novelty});
        }
        // SPEC §3.7.2 normative MUST: salience-ranked admission with a deterministic
        // **tie-break by template_id**, so a given input under a matching retention_profile
        // yields a bit-identical reservoir. Pinned by ReservoirTest.TieBreakByTemplateIdAtEqualSalience.
        std::ranges::sort(candidates,
                          [&ordered](const Candidate& lhs, const Candidate& rhs)
                          {
                              if (lhs.salience != rhs.salience)
                                  return lhs.salience > rhs.salience;
                              return ordered[lhs.index].first < ordered[rhs.index].first;
                          });
        // Admit in salience order, up to M total, capping exemplars PER KIND
        // (structural_role × dominant_level) for diversity (F10). A "kind" key
        // packs the two small enums into one integer for a cheap counter map.
        const auto kind_key{[](StructuralRole role, std::optional<LogLevel> level) noexcept
                            {
                                const auto lvl{level ? static_cast<std::uint16_t>(*level)
                                                     : std::uint16_t{0xFFU}};
                                return static_cast<std::uint16_t>(
                                    (static_cast<std::uint16_t>(role) << 8U) | lvl);
                            }};
        std::unordered_map<std::uint16_t, std::size_t> per_kind;
        stats.reservoir.reserve(std::min(config_.reservoir_size, candidates.size()));
        for (const auto& candidate : candidates)
        {
            if (stats.reservoir.size() >= config_.reservoir_size)
                break;
            const Bucket& bucket{*ordered[candidate.index].second};
            const auto level{dominant_level_of(bucket.level_counts)};
            const auto role{dominant_role_of(bucket.role_counts)};
            if (config_.reservoir_per_kind_cap > 0)
            {
                auto& kind_count{per_kind[kind_key(role, level)]};
                if (kind_count >= config_.reservoir_per_kind_cap)
                    continue; // this kind is already covered — keep M for other kinds
                ++kind_count;
            }
            ReservoirEntry entry;
            entry.template_id = ordered[candidate.index].first;
            if (config_.template_emission == TemplateEmissionMode::Inline)
                entry.template_str = bucket.template_str;
            entry.count = bucket.count;
            entry.frequency = total > 0.0 ? static_cast<double>(bucket.count) / total : 0.0;
            entry.dominant_level = level;
            entry.structural_role = role;
            entry.structural_surprise = candidate.structural_surprise;
            entry.novelty = candidate.novelty;
            entry.salience = candidate.salience;
            // §15.4 sub-coordinate: re-express the reconciled first-seen ordinal,
            // bounded by M. Only when a coordinate is configured (it is a sub-part
            // of the document coordinate; meaningless without one).
            if (config_.source_ref)
                entry.within_window_ordinal = bucket.first_seen_index;
            stats.reservoir.push_back(std::move(entry));
            reserved.insert(ordered[candidate.index].first);
        }
    }

    std::uint64_t tail_count = 0;
    std::uint64_t tail_max = 0;
    std::vector<std::uint64_t> tail_counts;
    if (ordered.size() > k)
        tail_counts.reserve(ordered.size() - k);
    for (std::size_t i = k; i < ordered.size(); ++i)
    {
        if (reserved.contains(ordered[i].first))
            continue; // promoted to the reservoir — excluded from tail aggregates (SPEC §3.7.3)
        const auto c = ordered[i].second->count;
        tail_count += c;
        if (c > tail_max)
            tail_max = c;
        tail_counts.push_back(c);
    }
    stats.tail_count = tail_count;
    stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

    // SPEC §3.6: tail_summary — emit when there is at least one tail
    // template. Entropy is normalised over the tail mass (NOT over
    // lines_observed_) so a few-template tail with one dominator
    // collapses cleanly toward 0 bits.
    if (stats.tail_unique > 0 && lines_observed_ > 0)
    {
        TailSummary ts;
        ts.tail_template_count = stats.tail_unique;
        ts.tail_entropy_bits = shannon_entropy_bits(tail_counts, tail_count);
        ts.tail_max_rate = static_cast<double>(tail_max) / static_cast<double>(lines_observed_);
        stats.tail_summary = ts;
    }

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

        // The bigram (transition) view — count(A→B) summed over the trailing
        // n-gram dimension — was already built above (it also feeds
        // structural_surprise); reuse it for branching + dominant_path. Reaching
        // this block (top_ngrams_size > 0 && ngram_total_ > 0) guarantees
        // need_graph held, so `transitions` is populated.
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

    return doc;
}

// ── JSON serialiser (glaze, DTO layer) ─────────────────────────
//
// Serialization is a thin DTO mirroring the MetaLog v0.5.0 envelope: each
// DTO field name IS the JSON key, so the struct declaration reads as the
// schema and glaze reflects it with zero stringly-typed mapping. The domain
// types stay free of any serialization concern.
//
// Restrictive/canonical output: every field that the spec omits-when-absent
// is a std::optional here, populated only when present; glaze's
// skip_null_members then drops it. One document -> one byte sequence.
namespace dto
{

struct Producer
{
    std::string name;
    std::string version;
    std::string implementation_uri;
};

struct Window
{
    std::string start;
    std::string end;
    std::uint64_t duration_seconds{0};
    std::uint64_t lines_observed{0};
};

// All fields optional: each omitted when absent. An all-empty source still
// serialises as `{}` (the block itself is always present in a document).
struct Source
{
    std::optional<std::string> service;
    std::optional<std::string> fleet;
    std::optional<std::uint64_t> host_count;
    std::optional<std::string> host;
    std::optional<std::map<std::string, std::string>> tags; // omitted when empty
};

struct TailSummary
{
    std::uint64_t tail_template_count{0};
    double tail_entropy_bits{0.0};
    double tail_max_rate{0.0};
};

// Per-wildcard-position value-count histogram (SPEC §3.5). value_counts is a
// std::map so glaze emits it KEY-SORTED — the §15.6 replay bit-identity
// requirement; the domain FieldHistogram::value_counts is an unordered_map whose
// iteration order is not portable across runs/impls, so it is copied into a map
// at the serialization boundary. approximate_cardinality is omitted when 0
// (== not computed). Reflected (no glaze meta): member names are the wire keys,
// matching $defs/param_histogram exactly.
struct ParamHistogram
{
    std::uint32_t param_index{0};
    std::map<std::string, std::uint64_t> value_counts;
    std::uint64_t total{0};
    double entropy_bits{0.0};
    std::optional<std::uint64_t> approximate_cardinality; // omit when 0 (not computed)
};

struct TopKEntry
{
    std::string template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<std::string> tmpl;  // key "template"; omitted when empty
    std::optional<std::string> level; // spec level string; omitted when absent
    // SPEC §3.5 per-param histograms. Present only when the producer enabled
    // max_param_histograms (batch / full-fidelity path); omitted otherwise
    // (skip_null_members), so default and streaming documents are byte-unchanged.
    std::optional<std::vector<ParamHistogram>> param_histograms;

    // glaze rename: `tmpl` -> "template" (a C++ keyword), every other field by
    // reflection.
    struct glaze
    {
        using T = TopKEntry;
        static constexpr auto value =
            glz::object("template_id", &T::template_id, "count", &T::count, "frequency",
                        &T::frequency, "template", &T::tmpl, "level", &T::level,
                        "param_histograms", &T::param_histograms);
    };
};

// Salience reservoir entry (Tier 2, F1). Self-describing: carries WHY it was kept
// (salience + the per-axis bands) so a consumer/explainer can attribute it without
// the producer. Emitted under `compose()` and serialise→reparse (F8).
struct ReservoirEntry
{
    std::string template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<std::string> tmpl;             // key "template"; omitted when empty
    std::optional<std::string> level;            // spec level string; omitted when absent
    std::optional<std::string> structural_role;  // omitted when None
    std::uint32_t structural_surprise{0};
    std::uint32_t novelty{0};
    std::uint32_t salience{0};
    std::optional<std::uint64_t> within_window_ordinal; // §15.4 sub-coordinate; omit when absent

    struct glaze
    {
        using T = ReservoirEntry;
        static constexpr auto value = glz::object(
            "template_id", &T::template_id, "count", &T::count, "frequency", &T::frequency,
            "template", &T::tmpl, "level", &T::level, "structural_role", &T::structural_role,
            "structural_surprise", &T::structural_surprise, "novelty", &T::novelty, "salience",
            &T::salience, "within_window_ordinal", &T::within_window_ordinal);
    };
};

struct Stats
{
    std::uint64_t unique_templates{0};
    std::size_t top_k_size{0};
    std::uint64_t tail_count{0};
    std::uint64_t tail_unique{0};
    std::vector<TopKEntry> top_k;
    std::optional<double> entropy_bits;
    std::optional<TailSummary> tail_summary;
    // Omitted when the reservoir is empty (disabled / nothing salient below top_k).
    std::optional<std::vector<ReservoirEntry>> reservoir;
};

struct NGramEntry
{
    std::vector<std::string> sequence;
    std::uint64_t count{0};
    double probability{0.0};
};

struct BranchingEntry
{
    std::string template_id;
    std::uint64_t fanout{0};
    std::uint64_t total_outgoing{0};
    double entropy_bits{0.0};
};

struct Behavior
{
    std::size_t ngram_size{2};
    std::vector<NGramEntry> top_ngrams;
    std::size_t top_ngrams_size{0};
    std::optional<std::uint64_t> graph_edge_count;
    std::optional<std::vector<std::string>> dominant_path;
    std::optional<std::vector<BranchingEntry>> branching;
};

struct Stability
{
    std::string previous_window_end;
    double kl_divergence{0.0};
    double js_divergence{0.0};
    std::uint64_t new_templates{0};
    std::uint64_t vanished_templates{0};
    double stability_score{1.0};
};

// Re-derivation coordinate wire shape (SPEC §15). Field names == JSON keys via
// reflection; optionals + skip_null_members omit guarantee-2 aids / children when
// absent. Recursive: a composed coordinate carries the set of child coordinates.
struct SourceRef
{
    std::string resolver_kind;
    std::string handle;
};

struct Bounds
{
    std::uint64_t start_tick{0};
    std::uint64_t end_tick{0};
};

struct Coordinate
{
    // §15.2 XOR: a raw coordinate has source_ref + bounds (children absent); a
    // composed coordinate has children only. skip_null_members omits whichever
    // group is absent — consumers discriminate by the presence of `children`.
    std::optional<SourceRef> source_ref;
    std::optional<Bounds> bounds;
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> config_hash;
    std::optional<std::vector<Coordinate>> children;
};

struct ProvenanceWindow
{
    std::string start;
    std::string end;
};

struct Provenance
{
    ProvenanceWindow window;
    std::optional<Source> source; // omitted when empty
    std::uint64_t lines_observed{0};
    std::optional<std::string> document_id;
    std::optional<Coordinate> coordinate; // §15.5 — the raw child's coordinate
};

struct Document
{
    std::string metalog_version;
    Producer producer;
    Window window;
    Source source;
    std::optional<std::map<std::string, std::string>> templates; // omitted when empty
    Stats stats;
    std::optional<Behavior> behavior;
    std::optional<Stability> stability;
    std::optional<std::vector<Provenance>> provenance;
    std::optional<std::string> canonicalization_version; // §2.4 processing identifiers
    std::optional<std::string> retention_profile;
    std::optional<Coordinate> coordinate; // §15 re-derivation coordinate
};

// ── Diff DTO (SPEC §13) ──
struct DocRef
{
    ProvenanceWindow window;
    std::optional<std::string> document_id;
};

struct TemplateDelta
{
    std::string template_id;
    std::uint64_t previous_count{0};
    std::uint64_t current_count{0};
    std::int64_t delta{0};
    std::optional<double> previous_frequency;
    std::optional<double> current_frequency;
};

struct BranchingDelta
{
    std::string template_id;
    double previous_entropy_bits{0.0};
    double current_entropy_bits{0.0};
    double delta_bits{0.0};
};

struct NGramRateChange
{
    std::vector<std::string> sequence;
    double previous_probability{0.0};
    double current_probability{0.0};
    double delta{0.0};
};

struct NGramDelta
{
    std::size_t ngram_size{2};
    std::optional<std::vector<std::vector<std::string>>> new_ngrams;      // omit when empty
    std::optional<std::vector<std::vector<std::string>>> vanished_ngrams; // omit when empty
    std::optional<std::vector<NGramRateChange>> rate_changed;             // omit when empty
};

struct TailDelta
{
    std::uint64_t previous_tail_template_count{0};
    std::uint64_t current_tail_template_count{0};
    std::int64_t tail_template_count_delta{0};
    double previous_tail_entropy_bits{0.0};
    double current_tail_entropy_bits{0.0};
    double tail_entropy_bits_delta{0.0};
    double previous_tail_max_rate{0.0};
    double current_tail_max_rate{0.0};
    double tail_max_rate_delta{0.0};
};

struct Diff
{
    std::string diff_version;
    DocRef previous;
    DocRef current;
    std::optional<double> kl_divergence;
    std::optional<double> js_divergence;
    std::optional<double> stability_score;
    std::optional<std::vector<TemplateDelta>> template_deltas; // omit when empty
    std::optional<std::vector<std::string>> new_templates;     // omit when empty
    std::optional<std::vector<std::string>> vanished_templates;
    std::optional<std::vector<BranchingDelta>> branching_delta;
    std::optional<NGramDelta> ngram_delta;
    std::optional<TailDelta> tail_delta;
};

} // namespace dto

namespace
{

constexpr glz::opts kWriteOpts{.skip_null_members = true};

// Build the serialization Source DTO from the domain SourceBlock.
dto::Source make_source(const SourceBlock& src)
{
    dto::Source out;
    out.service = src.service;
    out.fleet = src.fleet;
    out.host_count = src.host_count;
    out.host = src.host;
    if (!src.tags.empty())
        out.tags = src.tags;
    return out;
}

[[nodiscard]] bool source_is_empty(const SourceBlock& src) noexcept
{
    return !src.service && !src.fleet && !src.host_count && !src.host && src.tags.empty();
}

// Map a domain re-derivation coordinate to its wire shape (§15), recursing into
// composed children.
dto::Coordinate make_coordinate(const ReDerivationCoordinate& coord)
{
    dto::Coordinate out;
    if (coord.source_ref)
        out.source_ref = dto::SourceRef{coord.source_ref->resolver_kind, coord.source_ref->handle};
    if (coord.bounds)
        out.bounds = dto::Bounds{coord.bounds->start_tick, coord.bounds->end_tick};
    out.canonicalization_version = coord.canonicalization_version;
    out.config_hash = coord.config_hash;
    if (coord.children)
    {
        std::vector<dto::Coordinate> kids;
        kids.reserve(coord.children->size());
        for (const auto& child : *coord.children)
            kids.push_back(make_coordinate(child));
        out.children = std::move(kids);
    }
    return out;
}

dto::Document make_document(const MetaLogDocument& doc)
{
    dto::Document out;
    out.metalog_version = doc.metalog_version;
    out.producer = {doc.producer.name, doc.producer.version, doc.producer.implementation_uri};
    out.window = {doc.window.start_iso, doc.window.end_iso, doc.window.duration_seconds,
                  doc.window.lines_observed};
    out.source = make_source(doc.source);
    if (!doc.templates.empty())
        out.templates = doc.templates;

    out.stats.unique_templates = doc.stats.unique_templates;
    out.stats.top_k_size = doc.stats.top_k_size;
    out.stats.tail_count = doc.stats.tail_count;
    out.stats.tail_unique = doc.stats.tail_unique;
    out.stats.entropy_bits = doc.stats.entropy_bits;
    out.stats.top_k.reserve(doc.stats.top_k.size());
    for (const auto& entry : doc.stats.top_k)
    {
        dto::TopKEntry row;
        row.template_id = entry.template_id;
        row.count = entry.count;
        row.frequency = entry.frequency;
        // SPEC §3.4: inline `template` is optional.
        if (!entry.template_str.empty())
            row.tmpl = entry.template_str;
        if (entry.dominant_level)
            row.level = level_to_spec_string(*entry.dominant_level);
        // SPEC §3.5: per-param histograms (batch / full-fidelity path). Empty
        // unless the producer enabled max_param_histograms. field_histograms is
        // materialised in param_index order (close_window), so the slot order is
        // already deterministic; value_counts is copied into a std::map so the
        // wire is key-sorted (§15.6).
        if (!entry.field_histograms.empty())
        {
            std::vector<dto::ParamHistogram> hists;
            hists.reserve(entry.field_histograms.size());
            for (const auto& fh : entry.field_histograms)
            {
                dto::ParamHistogram ph;
                ph.param_index = fh.param_index;
                ph.value_counts = {fh.value_counts.begin(), fh.value_counts.end()};
                ph.total = fh.total;
                ph.entropy_bits = fh.entropy_bits;
                if (fh.approximate_cardinality > 0)
                    ph.approximate_cardinality = fh.approximate_cardinality;
                hists.push_back(std::move(ph));
            }
            row.param_histograms = std::move(hists);
        }
        out.stats.top_k.push_back(std::move(row));
    }
    if (doc.stats.tail_summary)
        out.stats.tail_summary = dto::TailSummary{doc.stats.tail_summary->tail_template_count,
                                                  doc.stats.tail_summary->tail_entropy_bits,
                                                  doc.stats.tail_summary->tail_max_rate};

    // Salience reservoir (F8): part of the external contract so a serialised metalog
    // document carries the rare-salient templates (and why they were kept). Omitted
    // when empty.
    if (!doc.stats.reservoir.empty())
    {
        std::vector<dto::ReservoirEntry> rows;
        rows.reserve(doc.stats.reservoir.size());
        for (const auto& entry : doc.stats.reservoir)
        {
            dto::ReservoirEntry row;
            row.template_id = entry.template_id;
            row.count = entry.count;
            row.frequency = entry.frequency;
            if (!entry.template_str.empty())
                row.tmpl = entry.template_str;
            if (entry.dominant_level)
                row.level = level_to_spec_string(*entry.dominant_level);
            if (entry.structural_role != StructuralRole::None)
                row.structural_role = std::string{to_string(entry.structural_role)};
            row.structural_surprise = entry.structural_surprise;
            row.novelty = entry.novelty;
            row.salience = entry.salience;
            row.within_window_ordinal = entry.within_window_ordinal;
            rows.push_back(std::move(row));
        }
        out.stats.reservoir = std::move(rows);
    }

    if (doc.behavior)
    {
        const auto& bh = *doc.behavior;
        dto::Behavior out_bh;
        out_bh.ngram_size = bh.ngram_size;
        out_bh.top_ngrams_size = bh.top_ngrams_size;
        out_bh.top_ngrams.reserve(bh.top_ngrams.size());
        for (const auto& ng : bh.top_ngrams)
            out_bh.top_ngrams.push_back({ng.sequence, ng.count, ng.probability});
        out_bh.graph_edge_count = bh.graph_edge_count;
        if (bh.dominant_path && !bh.dominant_path->empty())
            out_bh.dominant_path = *bh.dominant_path;
        if (bh.branching && !bh.branching->empty())
        {
            std::vector<dto::BranchingEntry> rows;
            rows.reserve(bh.branching->size());
            for (const auto& b : *bh.branching)
                rows.push_back({b.template_id, b.fanout, b.total_outgoing, b.entropy_bits});
            out_bh.branching = std::move(rows);
        }
        out.behavior = std::move(out_bh);
    }

    if (doc.stability)
    {
        const auto& sb = *doc.stability;
        out.stability = dto::Stability{sb.previous_window_end_iso, sb.kl_divergence,
                                       sb.js_divergence,           sb.new_templates,
                                       sb.vanished_templates,      sb.stability_score};
    }

    if (doc.provenance && !doc.provenance->empty())
    {
        std::vector<dto::Provenance> prov;
        prov.reserve(doc.provenance->size());
        for (const auto& p : *doc.provenance)
        {
            dto::Provenance row;
            row.window = {p.window_start_iso, p.window_end_iso};
            if (!source_is_empty(p.source))
                row.source = make_source(p.source);
            row.lines_observed = p.lines_observed;
            row.document_id = p.document_id;
            if (p.coordinate)
                row.coordinate = make_coordinate(*p.coordinate);
            prov.push_back(std::move(row));
        }
        out.provenance = std::move(prov);
    }
    out.canonicalization_version = doc.canonicalization_version;
    out.retention_profile = doc.retention_profile;
    if (doc.coordinate)
        out.coordinate = make_coordinate(*doc.coordinate);
    return out;
}

dto::DocRef make_doc_ref(const DocumentRef& ref)
{
    return {{ref.window_start_iso, ref.window_end_iso}, ref.document_id};
}

dto::Diff make_diff(const MetaLogDiff& d)
{
    dto::Diff out;
    out.diff_version = d.diff_version;
    out.previous = make_doc_ref(d.previous);
    out.current = make_doc_ref(d.current);
    out.kl_divergence = d.kl_divergence;
    out.js_divergence = d.js_divergence;
    out.stability_score = d.stability_score;

    if (!d.template_deltas.empty())
    {
        std::vector<dto::TemplateDelta> deltas;
        deltas.reserve(d.template_deltas.size());
        for (const auto& t : d.template_deltas)
            deltas.push_back({t.template_id, t.previous_count, t.current_count, t.delta,
                              t.previous_frequency, t.current_frequency});
        out.template_deltas = std::move(deltas);
    }
    if (!d.new_templates.empty())
        out.new_templates = d.new_templates;
    if (!d.vanished_templates.empty())
        out.vanished_templates = d.vanished_templates;
    if (!d.branching_delta.empty())
    {
        std::vector<dto::BranchingDelta> deltas;
        deltas.reserve(d.branching_delta.size());
        for (const auto& b : d.branching_delta)
            deltas.push_back(
                {b.template_id, b.previous_entropy_bits, b.current_entropy_bits, b.delta_bits});
        out.branching_delta = std::move(deltas);
    }
    if (d.ngram_delta)
    {
        dto::NGramDelta nd;
        nd.ngram_size = d.ngram_delta->ngram_size;
        if (!d.ngram_delta->new_ngrams.empty())
            nd.new_ngrams = d.ngram_delta->new_ngrams;
        if (!d.ngram_delta->vanished_ngrams.empty())
            nd.vanished_ngrams = d.ngram_delta->vanished_ngrams;
        if (!d.ngram_delta->rate_changed.empty())
        {
            std::vector<dto::NGramRateChange> changes;
            changes.reserve(d.ngram_delta->rate_changed.size());
            for (const auto& r : d.ngram_delta->rate_changed)
                changes.push_back(
                    {r.sequence, r.previous_probability, r.current_probability, r.delta});
            nd.rate_changed = std::move(changes);
        }
        out.ngram_delta = std::move(nd);
    }
    if (d.tail_delta)
    {
        const auto& t = *d.tail_delta;
        out.tail_delta = dto::TailDelta{
            t.previous_tail_template_count, t.current_tail_template_count,
            t.tail_template_count_delta,    t.previous_tail_entropy_bits,
            t.current_tail_entropy_bits,    t.tail_entropy_bits_delta,
            t.previous_tail_max_rate,       t.current_tail_max_rate,
            t.tail_max_rate_delta};
    }
    return out;
}

} // namespace

std::string to_json(const MetaLogDocument& doc)
{
    const dto::Document out{make_document(doc)};
    std::string buf;
    // Serialising a fully-formed value to a growable string cannot fail.
    (void)glz::write<kWriteOpts>(out, buf);
    return buf;
}

std::string to_json(const MetaLogDiff& diff)
{
    const dto::Diff out{make_diff(diff)};
    std::string buf;
    (void)glz::write<kWriteOpts>(out, buf);
    return buf;
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

// Fold a document's RESERVOIR mass into the same maps (F8). A template is disjoint
// across top_k/reservoir within one document, so this never double-counts an input;
// across inputs a template that is top_k in one and reservoir in the other gets its
// full merged count. Lets the composed top_k ranking and the re-derived reservoir
// see the rare-salient templates' counts, which `aggregate_top_k` alone misses.
void aggregate_reservoir(std::unordered_map<std::string, std::uint64_t>& counts,
                         std::unordered_map<std::string, std::string>& templates,
                         std::unordered_map<std::string, std::optional<LogLevel>>& levels,
                         const MetaLogDocument& doc)
{
    for (const auto& e : doc.stats.reservoir)
    {
        counts[e.template_id] += e.count;
        if (!e.template_str.empty() && !templates.contains(e.template_id))
            templates.emplace(e.template_id, e.template_str);
        if (e.dominant_level && !levels.contains(e.template_id))
            levels.emplace(e.template_id, e.dominant_level);
    }
}

// §2.4 comparability gate. When both sides carry the identifier, the values MUST
// be equal; throwing satisfies the spec's "MUST fail" branch. When one side omits
// it, the operation MAY proceed (the consumer should treat the result with
// caution); see check_processing_identifier_gate's callers for the carry rule.
void check_processing_identifier_gate(const std::optional<std::string>& lhs,
                                      const std::optional<std::string>& rhs,
                                      std::string_view field, std::string_view op)
{
    if (lhs && rhs && *lhs != *rhs)
        throw std::invalid_argument{
            std::string{"metalog::"} + std::string{op} + ": incompatible " + std::string{field} +
            " — \"" + *lhs + "\" vs \"" + *rhs + "\" (SPEC §2.4 comparability gate)"};
}

// §3.5 / §12.1 compose-carry of param_histograms. For each (template_id,
// param_index) in both inputs: union value_counts (sum on matching keys),
// truncate to the cap (top-N by count, deterministic tie-break by key),
// total = a.total + b.total, recompute entropy_bits over the merged counts,
// approximate_cardinality = max (HLL sketch is not in the doc — we don't have
// the registers to union; max is the spec's conservative fallback). For a slot
// present in only ONE input we carry it unchanged (§12.1: MAY carry or omit).
[[nodiscard]] std::vector<FieldHistogram>
merge_field_histograms(const std::vector<FieldHistogram>& lhs,
                       const std::vector<FieldHistogram>& rhs, std::size_t cap)
{
    std::unordered_map<std::uint32_t, const FieldHistogram*> lhs_index;
    lhs_index.reserve(lhs.size());
    for (const auto& fh : lhs)
        lhs_index.emplace(fh.param_index, &fh);

    std::vector<FieldHistogram> out;
    out.reserve(lhs.size() + rhs.size());
    std::unordered_set<std::uint32_t> seen;
    seen.reserve(rhs.size());

    for (const auto& b : rhs)
    {
        seen.insert(b.param_index);
        const auto found{lhs_index.find(b.param_index)};
        if (found == lhs_index.end())
        {
            out.push_back(b);
            continue;
        }
        const FieldHistogram& a{*found->second};
        FieldHistogram merged;
        merged.param_index = b.param_index;
        merged.total = a.total + b.total;
        std::unordered_map<std::string, std::uint64_t> values;
        values.reserve(a.value_counts.size() + b.value_counts.size());
        for (const auto& [v, c] : a.value_counts)
            values[v] += c;
        for (const auto& [v, c] : b.value_counts)
            values[v] += c;
        if (cap > 0 && values.size() > cap)
        {
            std::vector<std::pair<std::string, std::uint64_t>> sorted(values.begin(), values.end());
            std::ranges::partial_sort(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(cap),
                                      [](const auto& x, const auto& y)
                                      {
                                          if (x.second != y.second)
                                              return x.second > y.second;
                                          return x.first < y.first; // deterministic tie-break
                                      });
            values.clear();
            values.reserve(cap);
            for (std::size_t i{0}; i < cap; ++i)
                values.emplace(std::move(sorted[i].first), sorted[i].second);
        }
        std::vector<std::uint64_t> counts;
        counts.reserve(values.size());
        for (const auto& [v, c] : values)
            counts.push_back(c);
        merged.value_counts = std::move(values);
        merged.entropy_bits = shannon_entropy_bits(counts, merged.total);
        merged.approximate_cardinality =
            std::max(a.approximate_cardinality, b.approximate_cardinality);
        out.push_back(std::move(merged));
    }
    for (const auto& a : lhs)
        if (!seen.contains(a.param_index))
            out.push_back(a);
    std::ranges::sort(out, [](const auto& x, const auto& y)
                     { return x.param_index < y.param_index; });
    return out;
}

// Carry an identifier into a compose() output only when BOTH inputs supplied it
// (and matched — already checked). When only one side has it, omitting from the
// output is honest: the merged document covers an input under an unstated
// contract; consumers see the absence rather than an over-claim.
[[nodiscard]] std::optional<std::string>
carry_processing_identifier(const std::optional<std::string>& lhs,
                            const std::optional<std::string>& rhs)
{
    return (lhs && rhs && *lhs == *rhs) ? lhs : std::nullopt;
}

} // namespace

MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    // §2.4 gate fires BEFORE any merging — incompatible inputs MUST fail loudly,
    // not produce a hybrid document the consumer can't reason about.
    check_processing_identifier_gate(lhs.canonicalization_version, rhs.canonicalization_version,
                                     "canonicalization_version", "compose");
    check_processing_identifier_gate(lhs.retention_profile, rhs.retention_profile,
                                     "retention_profile", "compose");

    MetaLogDocument out;
    out.metalog_version = lhs.metalog_version;
    out.producer = lhs.producer;
    out.canonicalization_version =
        carry_processing_identifier(lhs.canonicalization_version, rhs.canonicalization_version);
    out.retention_profile =
        carry_processing_identifier(lhs.retention_profile, rhs.retention_profile);
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
    aggregate_reservoir(counts, templates, levels, lhs);
    aggregate_reservoir(counts, templates, levels, rhs);

    out.stats.top_k_size = lhs.stats.top_k_size;

    std::vector<std::pair<std::string, std::uint64_t>> ordered(counts.begin(), counts.end());
    std::ranges::sort(ordered,
                      [](const auto& a, const auto& b)
                      {
                          if (a.second != b.second)
                              return a.second > b.second;
                          return a.first < b.first;
                      });

    // Index each input's top_k by template_id for the §3.5 / §12.1
    // param_histograms compose-carry below.
    const auto build_topk_index{[](const MetaLogDocument& doc) {
        std::unordered_map<std::string, const TopKEntry*> index;
        index.reserve(doc.stats.top_k.size());
        for (const auto& entry : doc.stats.top_k)
            index.emplace(entry.template_id, &entry);
        return index;
    }};
    const auto lhs_topk{build_topk_index(lhs)};
    const auto rhs_topk{build_topk_index(rhs)};
    static constexpr std::size_t kComposeHistogramCap{
        MetaLogConfig::kDefaultMaxHistogramValues};

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
        // §3.5 / §12.1 compose-visible param_histograms. Look up the matching
        // entries in each input and merge per-param; one-sided is carried; entirely
        // absent → no histograms (consistent with the cap being a no-op when input
        // producers didn't emit any). Closes the F8/F2-value compose gap.
        const auto lhs_entry_it{lhs_topk.find(e.template_id)};
        const auto rhs_entry_it{rhs_topk.find(e.template_id)};
        const std::vector<FieldHistogram> empty{};
        const auto& lhs_hists{lhs_entry_it != lhs_topk.end() ? lhs_entry_it->second->field_histograms
                                                             : empty};
        const auto& rhs_hists{rhs_entry_it != rhs_topk.end() ? rhs_entry_it->second->field_histograms
                                                             : empty};
        if (!lhs_hists.empty() || !rhs_hists.empty())
            e.field_histograms = merge_field_histograms(lhs_hists, rhs_hists, kComposeHistogramCap);
        out.stats.top_k.push_back(std::move(e));
    }

    out.stats.unique_templates = ordered.size();

    // ── Salience reservoir re-derivation (F8; SPEC §3.7.3 / §12.1) ──
    // Carry the rare-salient templates through composition instead of dropping them
    // into the tail (the multi-scale gap: composed/pyramid baselines were blind to a
    // lone fatal / off-path branch). Candidates are templates that were salient in
    // EITHER input's reservoir and did not rise into the composed top_k by frequency.
    // structural_surprise/novelty carry through as the max across inputs; salience is
    // RE-DERIVED over the merged count + composed line total (rarity shifts on merge),
    // so the ranking reflects the composed window, not either input's. Bounded by the
    // inputs' (already diversity-capped) reservoirs.
    std::unordered_set<std::string> reserved;
    {
        struct SalienceInfo
        {
            std::uint32_t structural_surprise{0};
            std::uint32_t novelty{0};
            StructuralRole role{StructuralRole::None};
        };
        std::unordered_map<std::string, SalienceInfo> sal_info;
        const auto absorb_reservoir{[&](const MetaLogDocument& doc)
                                    {
                                        for (const auto& e : doc.stats.reservoir)
                                        {
                                            auto& info{sal_info[e.template_id]};
                                            info.structural_surprise = std::max(
                                                info.structural_surprise, e.structural_surprise);
                                            info.novelty = std::max(info.novelty, e.novelty);
                                            if (info.role == StructuralRole::None)
                                                info.role = e.structural_role;
                                        }
                                    }};
        absorb_reservoir(lhs);
        absorb_reservoir(rhs);

        std::unordered_set<std::string> topk_ids;
        topk_ids.reserve(out.stats.top_k.size());
        for (const auto& e : out.stats.top_k)
            topk_ids.insert(e.template_id);

        struct ResCand
        {
            std::string template_id;
            std::uint32_t salience;
            std::uint32_t structural_surprise;
            std::uint32_t novelty;
        };
        std::vector<ResCand> res_cands;
        for (const auto& [tid, info] : sal_info)
        {
            if (topk_ids.contains(tid))
                continue; // rose into top_k by merged frequency — not a reservoir entry
            const auto cit{counts.find(tid)};
            const std::uint64_t cnt{cit != counts.end() ? cit->second : 0};
            const auto lit{levels.find(tid)};
            const std::optional<LogLevel> lvl{lit != levels.end() ? lit->second : std::nullopt};
            const auto tit{templates.find(tid)};
            const std::string_view tstr{tit != templates.end() ? std::string_view{tit->second}
                                                               : std::string_view{}};
            const auto sal{salience_score(lvl, info.role, tstr, cnt, out.window.lines_observed,
                                          info.structural_surprise, info.novelty)};
            if (sal > 0U)
                res_cands.push_back(ResCand{.template_id = tid,
                                            .salience = sal,
                                            .structural_surprise = info.structural_surprise,
                                            .novelty = info.novelty});
        }
        std::ranges::sort(res_cands,
                          [](const ResCand& a, const ResCand& b)
                          {
                              if (a.salience != b.salience)
                                  return a.salience > b.salience;
                              return a.template_id < b.template_id;
                          });
        out.stats.reservoir.reserve(res_cands.size());
        for (const auto& cand : res_cands)
        {
            ReservoirEntry entry;
            entry.template_id = cand.template_id;
            if (auto t{templates.find(cand.template_id)}; t != templates.end())
                entry.template_str = t->second;
            const auto cit{counts.find(cand.template_id)};
            entry.count = cit != counts.end() ? cit->second : 0;
            entry.frequency =
                total_lines > 0.0 ? static_cast<double>(entry.count) / total_lines : 0.0;
            if (auto l{levels.find(cand.template_id)}; l != levels.end())
                entry.dominant_level = l->second;
            entry.structural_role = sal_info[cand.template_id].role;
            entry.structural_surprise = cand.structural_surprise;
            entry.novelty = cand.novelty;
            entry.salience = cand.salience;
            out.stats.reservoir.push_back(std::move(entry));
            reserved.insert(cand.template_id);
        }
    }

    std::uint64_t tail_count = 0;
    std::uint64_t tail_max = 0;
    std::vector<std::uint64_t> tail_counts;
    if (ordered.size() > k)
        tail_counts.reserve(ordered.size() - k);
    for (std::size_t i = k; i < ordered.size(); ++i)
    {
        if (reserved.contains(ordered[i].first))
            continue; // promoted to the reservoir — excluded from tail aggregates (SPEC §3.7.3)
        const auto c = ordered[i].second;
        tail_count += c;
        if (c > tail_max)
            tail_max = c;
        tail_counts.push_back(c);
    }
    out.stats.tail_count =
        tail_count + lhs.stats.tail_count + rhs.stats.tail_count; // approximate (SPEC §12.3)
    // Excludes templates promoted into the reservoir (not double-counted in the tail).
    out.stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

    // SPEC §3.6 + §12.3: tail_summary is recomputed from the merged
    // tail directly (not aggregated from inputs) so it stays exact for
    // the templates we actually have counts for. Inputs' own tail
    // masses (lhs.stats.tail_count / rhs.stats.tail_count) are
    // collapsed into a residual bucket so the entropy reflects "how
    // concentrated is the visible tail vs the lumped residuals".
    if (out.stats.tail_unique > 0 && out.window.lines_observed > 0)
    {
        TailSummary ts;
        ts.tail_template_count = out.stats.tail_unique;
        std::vector<std::uint64_t> entropy_counts = tail_counts;
        const std::uint64_t residual = lhs.stats.tail_count + rhs.stats.tail_count;
        if (residual > 0)
            entropy_counts.push_back(residual);
        const std::uint64_t denom = tail_count + residual;
        ts.tail_entropy_bits = shannon_entropy_bits(entropy_counts, denom);
        ts.tail_max_rate =
            static_cast<double>(tail_max) / static_cast<double>(out.window.lines_observed);
        out.stats.tail_summary = ts;
    }

    // Templates dedup map: union (matches SPEC §12).
    for (auto& [tid, tstr] : templates)
        out.templates.emplace(tid, std::move(tstr));

    // Stability dropped per SPEC §12.1.

    // Provenance: a composed document always carries it (SPEC §12.4). Extend
    // with the inputs' own provenance when present; otherwise this is a
    // first-level composition and we record both inputs directly.
    std::vector<ProvenanceEntry> prov;
    if (lhs.provenance)
        prov = *lhs.provenance;
    if (rhs.provenance)
        prov.insert(prov.end(), rhs.provenance->begin(), rhs.provenance->end());
    if (prov.empty())
    {
        prov.push_back({lhs.window.start_iso, lhs.window.end_iso, lhs.source,
                        lhs.window.lines_observed, std::nullopt, lhs.coordinate});
        prov.push_back({rhs.window.start_iso, rhs.window.end_iso, rhs.source,
                        rhs.window.lines_observed, std::nullopt, rhs.coordinate});
    }
    out.provenance = std::move(prov);

    // §15.5 re-derivation coordinate on a composed document: the SET of its raw
    // children's coordinates — never a coarse single [first,last] bound. Flatten
    // each input to leaves (a composed input contributes its own children), so the
    // composed coordinate always resolves to raw children, never an intermediate.
    std::vector<ReDerivationCoordinate> child_coords;
    const auto collect_leaves{[&child_coords](const MetaLogDocument& doc)
                              {
                                  if (!doc.coordinate)
                                      return;
                                  if (doc.coordinate->children)
                                      child_coords.insert(child_coords.end(),
                                                          doc.coordinate->children->begin(),
                                                          doc.coordinate->children->end());
                                  else
                                      child_coords.push_back(*doc.coordinate);
                              }};
    collect_leaves(lhs);
    collect_leaves(rhs);
    if (!child_coords.empty())
    {
        // §15.2 COMPOSED coordinate: children present, source_ref + bounds ABSENT
        // (no sentinel — §15.2 explicitly forbids them). Consumers discriminate by
        // the presence of `children`.
        ReDerivationCoordinate composed;
        composed.children = std::move(child_coords);
        out.coordinate = std::move(composed);
    }

    // Behavior: best-effort merge of top_ngrams by summing counts on
    // identical sequences. Branching/dominant_path/graph_edge_count are
    // intentionally NOT re-derived here: structural signals are diffed at
    // RAW pyramid scales (raw_strides), never against composed baselines, so
    // recomputing them on a composed document would add an unconsumed (and
    // potentially misleading) view. The rare-salient STRUCTURE that must
    // survive composition rides the reservoir above (structural_surprise is
    // carried per entry), which is what closes the multi-scale gap (F8).
    // Producers needing a full fresh graph on a composed document re-ingest raw.
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
    // §2.4 comparability gate (§13): a diff across mismatched processing contracts
    // is not meaningful — the documents fingerprint different rules. MUST fail.
    check_processing_identifier_gate(previous.canonicalization_version,
                                     current.canonicalization_version,
                                     "canonicalization_version", "diff");
    check_processing_identifier_gate(previous.retention_profile, current.retention_profile,
                                     "retention_profile", "diff");

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
    if (previous.behavior && current.behavior && previous.behavior->branching &&
        !previous.behavior->branching->empty() && current.behavior->branching &&
        !current.behavior->branching->empty())
    {
        std::unordered_map<std::string, double> prev_h;
        for (const auto& b : *previous.behavior->branching)
            prev_h[b.template_id] = b.entropy_bits;
        std::unordered_map<std::string, double> cur_h;
        for (const auto& b : *current.behavior->branching)
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
                // Per-side sample size backing the JS estimate (confidence basis).
                fhd.previous_sample_count = prev_fh->total;
                fhd.current_sample_count = curr_fh.total;
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

    // tail_delta: pairwise change in long-tail shape. Only when BOTH documents
    // carry a tail_summary (a one-sided tail is appearance/vanishing, expressed
    // by the template-level signals). Stateless before/after/delta; consumers
    // decide significance (the "louder AND more concentrated" rule is theirs).
    if (previous.stats.tail_summary && current.stats.tail_summary)
    {
        const auto& prev_tail = *previous.stats.tail_summary;
        const auto& cur_tail = *current.stats.tail_summary;
        TailDelta td;
        td.previous_tail_template_count = prev_tail.tail_template_count;
        td.current_tail_template_count = cur_tail.tail_template_count;
        td.tail_template_count_delta = static_cast<std::int64_t>(cur_tail.tail_template_count) -
                                       static_cast<std::int64_t>(prev_tail.tail_template_count);
        td.previous_tail_entropy_bits = prev_tail.tail_entropy_bits;
        td.current_tail_entropy_bits = cur_tail.tail_entropy_bits;
        td.tail_entropy_bits_delta = cur_tail.tail_entropy_bits - prev_tail.tail_entropy_bits;
        td.previous_tail_max_rate = prev_tail.tail_max_rate;
        td.current_tail_max_rate = cur_tail.tail_max_rate;
        td.tail_max_rate_delta = cur_tail.tail_max_rate - prev_tail.tail_max_rate;
        out.tail_delta = td;
    }

    return out;
}

} // namespace insight::metalog

// NOLINTEND
