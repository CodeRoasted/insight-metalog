module;
#include <picosha2.h>

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.cube;

// MetaLog producer engine (SPEC v0.5.0). The stateful streaming side: one window
// of CanonicalEvents in (open_window / ingest_event) -> one bounded MetaLog
// document out (close_window). Single responsibility — production; serialization,
// compose and diff live in their own translation units, and the cross-cutting
// statistics / salience / wire-format helpers live under detail/.

namespace insight::metalog
{

namespace
{
constexpr std::size_t kHashLeftShift{6U};
constexpr std::size_t kHashRightShift{2U};

[[nodiscard]] std::size_t mix(std::size_t seed, std::uint64_t value) noexcept
{
    constexpr std::size_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
    seed ^= static_cast<std::size_t>(value) + kGoldenRatio + (seed << kHashLeftShift) +
            (seed >> kHashRightShift);
    return seed;
}
} // namespace

// ── compute_template_id ────────────────────────────────────────

std::string MetaLogEngine::compute_template_id(std::string_view canonical_template)
{
    constexpr std::size_t kSha256Bytes = 32;
    std::array<unsigned char, kSha256Bytes> digest{};
    picosha2::hash256(canonical_template.begin(), canonical_template.end(), digest.begin(),
                      digest.end());

    // Spec §3.2: id = "h:" + lower_hex of the first 16 SHA-256 bytes (32 hex chars).
    constexpr std::size_t kTemplateIdBytes{16};
    constexpr unsigned kNibbleMask{0xFU};
    static constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7',
                                               '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out;
    out.reserve(2 + (2 * kTemplateIdBytes)); // "h:" + two hex chars per byte
    out.append("h:");
    for (std::size_t i = 0; i < kTemplateIdBytes; ++i)
    {
        const auto byte{static_cast<unsigned>(digest[i])};
        out.push_back(kHex[(byte >> 4) & kNibbleMask]);
        out.push_back(kHex[byte & kNibbleMask]);
    }
    return out;
}

// ── Engine ─────────────────────────────────────────────────────

// HyperLogLog pimpl implementation.
// Key = content_template_id + '\x1f' + decimal(param_index).
struct MetaLogEngine::HllState
{
    using HLL = HyperLogLog;

    std::unordered_map<std::string, std::vector<HLL>> sketches;
    // sketches[content_id][param_index]

    void reset()
    {
        sketches.clear();
    }

    void add(const std::string& content_id, std::size_t param_index, std::string_view value)
    {
        auto& slots = sketches[content_id];
        if (slots.size() <= param_index)
            slots.resize(param_index + 1);
        slots[param_index].add(value);
    }

    [[nodiscard]] std::uint64_t estimate(const std::string& content_id,
                                         std::size_t param_index) const noexcept
    {
        const auto found = sketches.find(content_id);
        if (found == sketches.end() || param_index >= found->second.size())
            return 0;
        return found->second[param_index].estimate();
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
    cube_base_.clear();
    (*hll_state_).reset();
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
    dst.first_seen_index = dst_existed ? std::min(dst.first_seen_index, moved.first_seen_index)
                                       : moved.first_seen_index;
    for (const auto& [level, level_count] : moved.level_counts)
        dst.level_counts[level] += level_count;
    for (const auto& [role, role_count] : moved.role_counts)
        dst.role_counts[role] += role_count;
    for (const auto& [component, component_count] : moved.component_counts)
        dst.component_counts[component] += component_count;

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
    ++bucket.role_counts[event.structural_role]; // announced role → salience
    ++lines_observed_;

    // SPEC §16 cube: accumulate the per-EVENT joint (level, component, role) and the
    // per-template component marginal (for the §16.6 reservoir cross). Gated on
    // emit_cube == false (default) → one predicted-not-taken branch, zero extra work on
    // the hot path (same discipline as max_param_histograms). The component string_view
    // is arena-stable only within the window, so it is COPIED into the keys.
    if (config_.emit_cube)
    {
        ++cube_base_[std::make_tuple(event.level, std::string{event.component},
                                     event.structural_role)];
        if (!event.component.empty())
            ++bucket.component_counts[std::string{event.component}];
    }

    // Per-param field histogram accumulation.
    // Gated on config_.max_param_histograms == 0 (default) → single
    // predicted-not-taken branch; zero extra work on the hot path.
    if (config_.max_param_histograms > 0 && !event.params.empty())
    {
        const std::size_t param_count{std::min(config_.max_param_histograms, event.params.size())};
        if (bucket.param_value_counts.size() < param_count)
        {
            bucket.param_value_counts.resize(param_count);
            bucket.param_totals.resize(param_count, 0);
        }
        for (std::size_t pi{0}; pi < param_count; ++pi)
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

MetaLogDocument MetaLogEngine::close_window(Timestamp end,
                                            std::optional<ReportedWindowBounds> reported_bounds)
{
    if (!window_start_)
        throw std::logic_error{"MetaLogEngine::close_window called before open_window"};

    MetaLogDocument doc;
    stamp_envelope(doc, *window_start_, end, reported_bounds);

    const WindowAnalysis analysis{analyze_window()};
    build_top_k(doc, analysis);

    std::unordered_set<std::string> reserved; // content_ids promoted to the reservoir
    build_reservoir(doc, analysis, reserved);
    build_tail_and_entropy(doc, analysis, reserved);

    build_behavior(doc, analysis);
    build_stability(doc, analysis);
    build_cube(doc); // SPEC §16 — only when config_.emit_cube

    // Carry this window's frequencies for the next window's stability, emit the
    // §3.4 dedup map, then drop the per-window state.
    stash_prev_window(doc);
    build_templates_map(doc);
    reset_window_state();

    return doc;
}

// Stamp the envelope: version/producer/source, window times + duration, the §2.4
// processing identifiers, and the §15 re-derivation coordinate (when configured).
void MetaLogEngine::stamp_envelope(MetaLogDocument& doc, Timestamp start, Timestamp end,
                                   std::optional<ReportedWindowBounds> reported_bounds) const
{
    doc.metalog_version = "0.6.0";
    doc.producer.version = config_.producer_version;
    doc.source = source_;

    // Reported bounds: the deterministic parseable-ts envelope when supplied (MUST 3),
    // else the open/close machinery times. Duration tracks the reported span.
    const Timestamp reported_start{reported_bounds ? reported_bounds->start : start};
    const Timestamp reported_end{reported_bounds ? reported_bounds->end : end};
    doc.window.start_iso = format_rfc3339_utc(reported_start);
    doc.window.end_iso = format_rfc3339_utc(reported_end);

    const auto delta{
        std::chrono::duration_cast<std::chrono::seconds>(reported_end - reported_start).count()};
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
        coord.bounds = EventTimeBounds{
            .start_tick = static_cast<std::uint64_t>(start.time_since_epoch().count()),
            .end_tick = static_cast<std::uint64_t>(end.time_since_epoch().count())};
        coord.canonicalization_version = config_.canonicalization_version;
        doc.coordinate = std::move(coord);
    }
}

// Cold-path scratch (RAII-owned by close_window): count-sorted bucket view +
// template transition graph + per-template structural-surprise band, built once
// before reservoir selection so structural_surprise can feed salience.
MetaLogEngine::WindowAnalysis MetaLogEngine::analyze_window() const
{
    WindowAnalysis analysis;
    // Sort buckets by count desc, template_id asc for determinism.
    auto& ordered = analysis.ordered;
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
    analysis.k = std::min(config_.top_k_size, ordered.size());
    build_transition_graph(analysis);
    return analysis;
}

// ── Transition graph + per-template structural surprise ──
// Built once (cold path) from the accumulated n-grams, BEFORE reservoir selection
// so structural_surprise can feed salience, then reused by the behavior block. A
// template's structural_surprise is the surprise of its MOST-LIKELY incoming
// transition: a node reachable only via a rare edge off the dominant path scores
// high even when its level/lexicon severity is 0 (the benign Info "took alternate
// cache path"). Integer-only (I5); the band depends solely on the winning edge's
// probability, so unordered_map iteration order cannot perturb it.
void MetaLogEngine::build_transition_graph(WindowAnalysis& analysis) const
{
    const bool need_graph{(config_.reservoir_size > 0 || config_.top_ngrams_size > 0) &&
                          ngram_total_ > 0};
    if (!need_graph)
        return;
    auto& transitions = analysis.transitions;
    auto& incoming_surprise = analysis.incoming_surprise;
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
        for (const auto& [to_id, count] : row)
            outgoing += count;
        if (outgoing == 0U)
            continue;
        for (const auto& [to_id, count] : row)
        {
            if (to_id >= node_count)
                continue;
            // Most-likely incoming edge = highest ratio count/outgoing (cross-multiplied, exact
            // integer). DETERMINISTIC tie-break on EQUAL ratio: prefer the edge with MORE observations
            // (larger count) — a pure function of the contents, NOT the unordered_map iteration order.
            // Two equal-ratio edges with different absolute (count, outgoing) can fall in DIFFERENT
            // surprise bands (the ≥2-observation floor + the integer thresholds in surprise_band), so an
            // order-dependent pick diverges across stdlibs and perturbs structural_surprise → the
            // salience ranking → the reservoir admission boundary. (Found via the §9.2 cross-count
            // clang≢gcc; the same determinism discipline as dominant_level_of/dominant_role_of.)
            const std::uint64_t cand{count * best_t[to_id]};
            const std::uint64_t best{best_c[to_id] * outgoing};
            if (cand > best || (cand == best && count > best_c[to_id]))
            {
                best_c[to_id] = count;
                best_t[to_id] = outgoing;
            }
        }
    }
    for (std::size_t id = 0; id < node_count; ++id)
        incoming_surprise[id] = surprise_band(best_c[id], best_t[id]);
}

// Structural surprise for a bucket's content_id (0 when the graph is absent or
// the template has no rare incoming edge).
std::uint32_t MetaLogEngine::surprise_of(const WindowAnalysis& analysis,
                                         const std::string& content_id) const noexcept
{
    if (analysis.incoming_surprise.empty())
        return 0U;
    const auto found{content_template_index_.find(content_id)};
    if (found == content_template_index_.end() ||
        found->second >= analysis.incoming_surprise.size())
        return 0U;
    return analysis.incoming_surprise[found->second];
}

void MetaLogEngine::build_top_k(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.k;
    const auto total{static_cast<double>(lines_observed_)};

    StatsBlock& stats = doc.stats;
    stats.unique_templates = ordered.size();
    stats.top_k_size = config_.top_k_size;
    stats.top_k.reserve(top_k_cut);

    for (std::size_t i = 0; i < top_k_cut; ++i)
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
            const auto& bucket{*ordered[i].second};
            const auto& content_id{ordered[i].first};
            for (std::size_t pi{0}; pi < bucket.param_value_counts.size(); ++pi)
            {
                FieldHistogram hist;
                hist.param_index = static_cast<std::uint32_t>(pi);
                hist.value_counts = bucket.param_value_counts[pi];
                hist.total = bucket.param_totals[pi];
                // Shannon entropy over the tracked values.
                // Note: when total > sum(value_counts) (cap was hit),
                // entropy is slightly under-estimated — known limitation.
                std::vector<std::uint64_t> vcounts;
                vcounts.reserve(hist.value_counts.size());
                for (const auto& [value, count] : hist.value_counts)
                    vcounts.push_back(count);
                hist.entropy_bits = shannon_entropy_bits(vcounts, hist.total);
                // HLL approximate cardinality (SPEC §3.5).
                hist.approximate_cardinality = hll_state_->estimate(content_id, pi);
                entry.field_histograms.push_back(std::move(hist));
            }
        }

        stats.top_k.push_back(std::move(entry));
    }
}

void MetaLogEngine::build_reservoir(MetaLogDocument& doc, const WindowAnalysis& analysis,
                                    std::unordered_set<std::string>& reserved) const
{
    // ── Tier 2: Salience Reservoir ──
    // From the below-top_k templates, retain the most SALIENT (not the most
    // frequent) — this is where a rare-but-severe event (a lone fatal) survives
    // instead of collapsing into the tail. Disjoint from top_k by construction
    // (candidates are ordered[k..]); admitted templates are excluded from the tail
    // residual so they are not double-counted.
    if (config_.reservoir_size == 0 || analysis.ordered.size() <= analysis.k)
        return;
    auto candidates = collect_reservoir_candidates(analysis);
    admit_reservoir(doc.stats, analysis, candidates, reserved);
}

// The below-top_k templates that score a non-zero (integer, deterministic)
// salience — the rarity-modulated severity ⊕ structure ⊕ novelty.
std::vector<MetaLogEngine::ReservoirCandidate>
MetaLogEngine::collect_reservoir_candidates(const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.k;
    std::vector<ReservoirCandidate> candidates;
    for (std::size_t i = top_k_cut; i < ordered.size(); ++i)
    {
        const Bucket& bucket{*ordered[i].second};
        const auto surprise{surprise_of(analysis, ordered[i].first)};
        const auto novelty{novelty_band(bucket.first_seen_index, lines_observed_, bucket.count)};
        const auto sal{salience_score(dominant_level_of(bucket.level_counts),
                                      dominant_role_of(bucket.role_counts), bucket.template_str,
                                      bucket.count, lines_observed_, surprise, novelty)};
        if (sal > 0U)
            candidates.push_back(ReservoirCandidate{
                .index = i, .salience = sal, .structural_surprise = surprise, .novelty = novelty});
    }
    return candidates;
}

// Admit candidates to the reservoir in salience order (SPEC §3.7.2 MUST: tie-break
// by template_id for a bit-identical reservoir), bounded by reservoir_size and the
// per-kind (structural_role × dominant_level) diversity cap.
void MetaLogEngine::admit_reservoir(StatsBlock& stats, const WindowAnalysis& analysis,
                                    std::vector<ReservoirCandidate>& candidates,
                                    std::unordered_set<std::string>& reserved) const
{
    const auto& ordered = analysis.ordered;
    const auto total{static_cast<double>(lines_observed_)};
    // SPEC §3.7.2 normative MUST: salience-ranked admission with a deterministic
    // **tie-break by template_id**, so a given input under a matching retention_profile
    // yields a bit-identical reservoir. Pinned by
    // ReservoirTest.TieBreakByTemplateIdAtEqualSalience.
    std::ranges::sort(candidates,
                      [&ordered](const ReservoirCandidate& lhs, const ReservoirCandidate& rhs)
                      {
                          if (lhs.salience != rhs.salience)
                              return lhs.salience > rhs.salience;
                          return ordered[lhs.index].first < ordered[rhs.index].first;
                      });
    // Admit in salience order, up to M total, capping exemplars PER KIND
    // (structural_role × dominant_level) for diversity. A "kind" key
    // packs the two small enums into one integer for a cheap counter map.
    constexpr unsigned kKindRoleShift{8U};
    const auto kind_key{
        [](StructuralRole role, std::optional<LogLevel> level) noexcept
        {
            const auto lvl{level ? static_cast<std::uint16_t>(*level) : std::uint16_t{0xFFU}};
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(role) << kKindRoleShift) |
                                              lvl);
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
        // §16.6 reservoir→cell cross: LOCATION-only {level, where}. A pure function of
        // the entry's (dominant level, dominant component) — read-only, one-way, carries
        // no salience. Only when a cube block is emitted.
        if (config_.emit_cube)
            entry.cube_coord =
                cube::cube_location(level, dominant_component_of(bucket.component_counts));
        stats.reservoir.push_back(std::move(entry));
        reserved.insert(ordered[candidate.index].first);
    }
}

void MetaLogEngine::build_tail_and_entropy(MetaLogDocument& doc, const WindowAnalysis& analysis,
                                           const std::unordered_set<std::string>& reserved) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.k;
    StatsBlock& stats = doc.stats;
    std::uint64_t tail_count = 0;
    std::uint64_t tail_max = 0;
    std::vector<std::uint64_t> tail_counts;
    if (ordered.size() > top_k_cut)
        tail_counts.reserve(ordered.size() - top_k_cut);
    for (std::size_t i = top_k_cut; i < ordered.size(); ++i)
    {
        if (reserved.contains(ordered[i].first))
            continue; // promoted to the reservoir — excluded from tail aggregates (SPEC §3.7.3)
        const auto count = ordered[i].second->count;
        tail_count += count;
        // — hot path: defensive clamp
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (count > tail_max)
            tail_max = count;
        tail_counts.push_back(count);
    }
    stats.tail_count = tail_count;
    stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

    // SPEC §3.6: tail_summary — emit when there is at least one tail
    // template. Entropy is normalised over the tail mass (NOT over
    // lines_observed_) so a few-template tail with one dominator
    // collapses cleanly toward 0 bits.
    if (stats.tail_unique > 0 && lines_observed_ > 0)
    {
        TailSummary summary;
        summary.tail_template_count = stats.tail_unique;
        summary.tail_entropy_bits = shannon_entropy_bits(tail_counts, tail_count);
        summary.tail_max_rate =
            static_cast<double>(tail_max) / static_cast<double>(lines_observed_);
        stats.tail_summary = summary;
    }

    // entropy_bits over the full (untruncated) template distribution.
    if (lines_observed_ > 0)
    {
        std::vector<std::uint64_t> counts;
        counts.reserve(ordered.size());
        for (const auto& entry : ordered)
            counts.push_back(entry.second->count);
        stats.entropy_bits = shannon_entropy_bits(counts, lines_observed_);
    }
}

void MetaLogEngine::build_behavior(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    // ── behavior block ──
    if (config_.top_ngrams_size == 0 || ngram_total_ == 0)
        return;

    BehaviorBlock behavior;
    behavior.ngram_size = config_.ngram_size;
    behavior.top_ngrams_size = config_.top_ngrams_size;
    build_top_ngrams(behavior);

    // graph_edge_count: count(A→B) edges from the transition view (reused from
    // analyze_window; reaching here guarantees `transitions` is populated).
    std::uint64_t edge_count = 0;
    for (const auto& [from, row] : analysis.transitions)
        edge_count += row.size();
    behavior.graph_edge_count = edge_count;

    build_branching(behavior, analysis);
    build_dominant_path(behavior, analysis);
    doc.behavior = std::move(behavior);
}

// top_ngrams (SPEC §4): the highest-count n-grams with p(last | prefix).
void MetaLogEngine::build_top_ngrams(BehaviorBlock& behavior) const
{
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
        const auto prefix_total{prefix_it == prefix_totals.end() ? 0 : prefix_it->second};
        entry.probability =
            prefix_total > 0 ? static_cast<double>(count) / static_cast<double>(prefix_total) : 0.0;
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
    behavior.top_ngrams = std::move(entries);
}

// branching (SPEC §4.2): per-source fanout + outgoing-transition entropy.
void MetaLogEngine::build_branching(BehaviorBlock& behavior, const WindowAnalysis& analysis) const
{
    if (config_.top_branching_size == 0)
        return;
    const auto& transitions = analysis.transitions;
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
        for (const auto& [_sinked, count] : row)
            total += count;
        entry.total_outgoing = total;
        if (total > 0)
        {
            // Branching entropy in the exact integer/count domain.
            insight::det::FixedReducer reducer;
            const std::int64_t log2_total{insight::det::det_log2_fixed(total)};
            for (const auto& [_sinked, count] : row)
            {
                if (count == 0)
                    continue;
                // det::i128 (canon shim: native __int128 on gcc/clang, portable struct on MSVC).
                // u64 count widened VALUE-PRESERVING via u128, matching native. [[msvc-port-stdlib-isms]]
                reducer.add_fixed(static_cast<insight::det::i128>(insight::det::u128{count}) *
                                  (log2_total - insight::det::det_log2_fixed(count)));
            }
            entry.entropy_bits = reducer.normalized_bits(static_cast<std::int64_t>(total));
        }
        branching_rows.push_back(std::move(entry));
    }
    std::ranges::sort(branching_rows,
                      [](const BranchingEntry& lhs, const BranchingEntry& rhs)
                      {
                          if (lhs.entropy_bits != rhs.entropy_bits)
                              return lhs.entropy_bits > rhs.entropy_bits;
                          if (lhs.total_outgoing != rhs.total_outgoing)
                              return lhs.total_outgoing > rhs.total_outgoing;
                          return lhs.template_id < rhs.template_id;
                      });
    if (branching_rows.size() > config_.top_branching_size)
        branching_rows.resize(config_.top_branching_size);
    behavior.branching = std::move(branching_rows);
}

// dominant_path (SPEC §4.1): greedy highest-count walk from the busiest template.
void MetaLogEngine::build_dominant_path(BehaviorBlock& behavior,
                                        const WindowAnalysis& analysis) const
{
    if (config_.dominant_path_max_steps == 0 || buckets_.empty())
        return;
    const auto& transitions = analysis.transitions;

    std::vector<std::string> path;
    std::unordered_set<InternalTemplateID> seen;
    path.reserve(config_.dominant_path_max_steps + 1U);
    seen.reserve(config_.dominant_path_max_steps + 1U);
    InternalTemplateID current = dominant_path_start();
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
            for (const auto& [to_id, count] : row_it->second)
            {
                if (count > best_to_count || (count == best_to_count && to_id < best_to))
                {
                    best_to_count = count;
                    best_to = to_id;
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
    behavior.dominant_path = std::move(path);
}

// Highest-count template (ties → lower internal id) — the dominant_path start node.
MetaLogEngine::InternalTemplateID MetaLogEngine::dominant_path_start() const
{
    InternalTemplateID start_id{0};
    std::uint64_t best_count{0};
    for (InternalTemplateID id = 0; id < content_templates_by_internal_id_.size(); ++id)
    {
        const auto& tid{content_templates_by_internal_id_[id]};
        auto bit{buckets_.find(tid)};
        if (bit == buckets_.end())
            continue;
        if (bit->second.count > best_count || (bit->second.count == best_count && id < start_id))
        {
            best_count = bit->second.count;
            start_id = id;
        }
    }
    return start_id;
}

void MetaLogEngine::build_stability(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
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

        const auto [kl_value,
                    js_value]{divergences(cur_freq, lines_observed_, prev_freq_, prev_total_)};
        const auto [added, gone]{new_and_vanished(cur_freq, prev_freq_)};

        StabilityBlock stability;
        stability.previous_window_end_iso = *prev_window_end_iso_;
        stability.kl_divergence = kl_value;
        stability.js_divergence = js_value;
        stability.new_templates = added;
        stability.vanished_templates = gone;
        stability.stability_score = 1.0 - js_value;
        // — hot path: defensive clamp [0,1] in the common case
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (stability.stability_score < 0.0)
            stability.stability_score = 0.0;
        // NOLINTNEXTLINE(readability-use-std-min-max) — hot path: defensive clamp
        if (stability.stability_score > 1.0)
            stability.stability_score = 1.0;
        doc.stability = std::move(stability);
    }
}

// Intra-window closed cube (SPEC §16): flatten the per-event (level, component, role)
// joint accumulated during ingest into BaseRows and close it. Built in batch over the
// frozen window → a pure function of that set, bit-identical cross-stdlib/OS (§16.9).
void MetaLogEngine::build_cube(MetaLogDocument& doc) const
{
    if (!config_.emit_cube)
        return;
    std::vector<cube::BaseRow> base;
    base.reserve(cube_base_.size());
    for (const auto& [key, count] : cube_base_)
    {
        const auto& [level, component, role]{key};
        base.push_back(cube::BaseRow{
            .level = level, .component = component, .role = role, .count = count});
    }
    doc.cube = cube::build_closed_cube(base);
    doc.has_cube = true;
}

void MetaLogEngine::stash_prev_window(const MetaLogDocument& doc)
{
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
}

void MetaLogEngine::build_templates_map(MetaLogDocument& doc) const
{
    // ── templates dedup map (SPEC §3.4) ──
    if (config_.template_emission == TemplateEmissionMode::Dedup)
    {
        // Emit every distinct template_id observed in the window
        // (including tail templates) so consumers can resolve any id
        // referenced by stats/behavior.
        for (const auto& [tid, bucket] : buckets_)
            doc.templates.emplace(tid, bucket.template_str);
    }
}

void MetaLogEngine::reset_window_state()
{
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
    cube_base_.clear();
}

} // namespace insight::metalog
