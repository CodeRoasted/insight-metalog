module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.cube;

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

// invariant: one sketch per (content_id, param_index); the per-content vector grows on demand.
struct MetaLogEngine::HllState
{
    using HLL = HyperLogLog;

    std::unordered_map<std::string, std::vector<HLL>> sketches;

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

// invariant: the cross-window state -- prev_freq_, prev_window_end_iso_, registry_ -- survives
// this call and feeds the stability block and the display vocabulary.
// note: the span-link and service-topology accumulators are cleared at close, not here.
void MetaLogEngine::open_window(Timestamp start)
{
    window_start_ = start;
    lines_observed_ = 0;
    buckets_.clear();
    template_str_cache_.clear();
    content_template_index_.clear();
    content_templates_by_internal_id_.clear();
    global_ring_ = {};
    trace_rings_.clear();
    trace_ring_fifo_.clear();
    // refs: SRC-D-OTEL-11
    span_templates_.clear();
    span_fifo_.clear();
    pending_span_edges_.clear();
    span_records_ = 0;
    orphan_parent_edges_ = 0;
    ngram_counts_.clear();
    ngram_total_ = 0;
    // note: the drop counter is per-window, like the table whose bound it records.
    // refs: ADR-9.D3
    ngram_observations_dropped_ = 0;
    cube_base_.clear();
    (*hll_state_).reset();
}

MetaLogEngine::TemplateLookup
MetaLogEngine::content_template_id_for(const tokenization::CanonicalEvent& event)
{
    // assert: template_str is the content-deterministic identity, so a cache hit returns without
    // recomputing the hash.
    if (auto cached{template_str_cache_.find(event.template_str)};
        cached != template_str_cache_.end())
    {
        return {.content_id = &cached->second.content_id,
                .internal_id = cached->second.internal_id};
    }

    // note: the domain carries the POD id; the rendered string is the engine's own map key.
    // refs: SRC-D-TIR-2
    const TemplateId template_id{insight::template_id_of(event.template_str)};
    std::string content_id{insight::render(template_id)};

    auto index_it{content_template_index_.find(content_id)};
    InternalTemplateID internal_id{};
    if (index_it == content_template_index_.end())
    {
        internal_id = static_cast<InternalTemplateID>(content_templates_by_internal_id_.size());
        content_templates_by_internal_id_.push_back(template_id);
        content_template_index_.emplace(content_id, internal_id);
        // invariant: the registry is the single home of the display-only template_str, interned
        // once per id and accumulating across windows so older documents resolve by id.
        // refs: SRC-D-TIR-5
        registry_.intern(template_id, event.template_str);
    }
    else
    {
        internal_id = index_it->second;
    }

    auto [iterator, inserted]{template_str_cache_.try_emplace(
        std::string{event.template_str},
        TemplateCacheEntry{.content_id = std::move(content_id), .internal_id = internal_id})};
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
        {
            // invariant: a refused key is COUNTED, so a consumer can tell a complete n-gram
            // distribution from a truncated one.
            // refs: ADR-9.D3
            ++ngram_observations_dropped_;
            return;
        }
        ngram_counts_.emplace(key, 1);
    }
    else
    {
        ++iterator->second;
    }
    ++ngram_total_;
}

void MetaLogEngine::account_ngram_into(NgramRing& ring, InternalTemplateID internal_id)
{
    // note: a bigram needs one prior id in the ring and a trigram two.
    if (config_.ngram_size == 2 && ring.filled >= 1)
    {
        NGramKey key{.size = 2};
        key.ids[0] = ring.recent[0];
        key.ids[1] = internal_id;
        account_ngram(key);
    }
    else if (config_.ngram_size == 3 && ring.filled >= 2)
    {
        NGramKey key{.size = 3};
        key.ids[0] = ring.recent[1];
        key.ids[1] = ring.recent[0];
        key.ids[2] = internal_id;
        account_ngram(key);
    }

    ring.recent[1] = ring.recent[0];
    ring.recent[0] = internal_id;
    if (ring.filled < 2)
        ++ring.filled;
}

MetaLogEngine::NgramRing& MetaLogEngine::trace_ring_for(TraceId trace_id)
{
    if (auto iterator{trace_rings_.find(trace_id)}; iterator != trace_rings_.end())
        return iterator->second;
    // invariant: the bound evicts the oldest-inserted trace, so eviction costs at most one
    // cross-record edge and never a membership.
    // note: max_active_traces == 0 disables the bound.
    if (config_.max_active_traces > 0 && trace_rings_.size() >= config_.max_active_traces &&
        !trace_ring_fifo_.empty())
    {
        trace_rings_.erase(trace_ring_fifo_.front());
        trace_ring_fifo_.pop_front();
    }
    trace_ring_fifo_.push_back(trace_id);
    return trace_rings_[trace_id];
}

void MetaLogEngine::record_span(const tokenization::CanonicalEvent& event,
                                InternalTemplateID internal_id)
{
    // note: the span vocabulary is spoken iff span_records_ > 0.
    // refs: SRC-D-OTEL-13
    ++span_records_;
    const SpanId span_id{event.trace.span_id};
    // invariant: a span id is unique per (trace, span); on a hash collision the first wins.
    // note: the FIFO evicts the oldest-inserted span under max_active_spans.
    if (!span_templates_.contains(span_id))
    {
        if (config_.max_active_spans > 0 && span_templates_.size() >= config_.max_active_spans &&
            !span_fifo_.empty())
        {
            span_templates_.erase(span_fifo_.front());
            span_fifo_.pop_front();
        }
        // refs: SRC-D-OTEL-21
        span_templates_.emplace(span_id, SpanNode{.template_id = internal_id,
                                                  .component = std::string{event.component}});
        span_fifo_.push_back(span_id);
    }
    // note: the parent resolves at close, since a child routinely serializes before it.
    if (event.trace.has_parent)
        pending_span_edges_.push_back({.child_template = internal_id,
                                       .parent_span_id = event.trace.parent_span_id,
                                       .child_component = std::string{event.component}});

    // refs: SRC-D-OTEL-9
    for (const SpanId linked : event.linked_span_ids)
        pending_link_edges_.push_back(
            {.source_component = std::string{event.component}, .linked_span_id = linked});
}

void MetaLogEngine::resolve_span_edges()
{
    for (const auto& edge : pending_span_edges_)
    {
        const auto parent_it{span_templates_.find(edge.parent_span_id)};
        if (parent_it == span_templates_.end())
        {
            // note: an unresolved parent is counted, never guessed.
            ++orphan_parent_edges_;
            continue;
        }
        // invariant: a declared span edge enters the SAME bounded graph as an inferred bigram, so
        // dominant_path and structural_surprise consume it transparently.
        NGramKey key{.size = 2};
        key.ids[0] = parent_it->second.template_id;
        key.ids[1] = edge.child_template;
        account_ngram(key);

        // note: an unknown endpoint and a self-edge are excluded -- neither is topology.
        // refs: SRC-D-OTEL-21
        const std::string& caller{parent_it->second.component};
        const std::string& callee{edge.child_component};
        if (!caller.empty() && !callee.empty() && caller != callee)
            ++service_edges_[{caller, callee}];
    }

    // note: a link whose target left the window yields no edge and is counted as an orphan.
    // refs: SRC-D-OTEL-9
    for (const auto& link : pending_link_edges_)
    {
        const auto target_it{span_templates_.find(link.linked_span_id)};
        if (target_it == span_templates_.end())
        {
            ++orphan_link_edges_;
            continue;
        }
        const std::string& caller{link.source_component};
        const std::string& callee{target_it->second.component};
        if (!caller.empty() && !callee.empty() && caller != callee)
            ++service_edges_[{caller, callee}];
    }
}

// note: NOLINT: one coherent hot-path accumulator; a split fragments the path's locality.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
        bucket.first_seen_index = lines_observed_;
    }
    ++bucket.count;
    ++bucket.level_counts[event.level];
    // invariant: the level and its declared-provenance counter are written off one event, so a
    // level can never be accumulated without its provenance.
    // refs: DN-32.D3
    if (event.declared_level)
        ++bucket.declared_level_counts[event.level];
    // invariant: all_echoed_source is an AND-reduction -- one runtime occurrence ends it.
    // refs: SRC-D-PROV-1
    ++bucket.role_counts[event.structural_role];
    bucket.all_echoed_source = bucket.all_echoed_source && event.echoed_source;
    ++lines_observed_;

    // invariant: the cube joint is accumulated unconditionally; the collapse guardrail bounds its
    // cardinality.
    // assert: the key copies on FIRST SIGHT only; a steady-state hit looks up by view.
    // refs: ADR-9.D2
    if (auto hit{
            cube_base_.find(std::make_tuple(event.level, event.component, event.structural_role))};
        hit != cube_base_.end())
        ++hit->second;
    else
        ++cube_base_[std::make_tuple(event.level, std::string{event.component},
                                     event.structural_role)];

    // note: an empty component is not counted, so records_with_component counts located rows.
    // refs: SRC-D-WHERE-2
    if (!event.component.empty())
    {
        // refs: ADR-9.D2
        if (auto hit{bucket.component_counts.find(event.component)};
            hit != bucket.component_counts.end())
            ++hit->second;
        else
            ++bucket.component_counts[std::string{event.component}];
    }

    // note: the ingest pipeline ships this enabled, so the key handling below is hot-path.
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
            const std::string_view val{event.params[pi]};
            // refs: ADR-9.D2
            if (auto hit{vcounts.find(val)}; hit != vcounts.end())
                ++hit->second;
            else if (vcounts.size() < config_.max_histogram_values)
                // assert: param_totals rose already, so total may exceed the sum of value_counts.
                ++vcounts[std::string{val}];

            // note: the HLL sketch is fed regardless of the value-table cap.
            hll_state_->add(*lookup.content_id, pi, val);
        }
    }

    // note: ordinal observations are field-keyed, so they never collide with param values.
    // refs: SRC-D-W1-2
    if (config_.max_param_histograms > 0 && !event.ordinals.empty())
    {
        for (const auto& observation : event.ordinals)
        {
            // note: try_emplace needs the key type, so a hit is a find and the emplace first-sight.
            // refs: ADR-9.D2
            auto ord_it{bucket.ordinal_accumulators.find(observation.field_name)};
            if (ord_it == bucket.ordinal_accumulators.end())
            {
                ord_it =
                    bucket.ordinal_accumulators.try_emplace(std::string{observation.field_name})
                        .first;
                ord_it->second.schedule = observation.schedule;
                ord_it->second.counts.assign(ordinal_schedule_bins(observation.schedule), 0U);
            }
            auto& accumulator{ord_it->second};
            const std::uint32_t bin{ordinal_bin_index(observation.schedule, observation.value)};
            if (bin < accumulator.counts.size())
            {
                ++accumulator.counts[bin];
                ++accumulator.total;
            }
        }
    }

    // invariant: an OTEL event forms its n-grams within its own trace ring, and both rings feed the
    // one bounded graph rather than a per-trace sub-fingerprint.
    // note: a SPAN record's causality is DECLARED and never enters an adjacency ring.
    // refs: ADR-29.D1, SRC-D-OTEL-11
    if (event.trace.is_span)
    {
        record_span(event, lookup.internal_id);
        return;
    }
    NgramRing& ring{(config_.trace_scoping_enabled && event.trace.present)
                        ? trace_ring_for(event.trace.trace_id)
                        : global_ring_};
    account_ngram_into(ring, lookup.internal_id);
}

MetaLogDocument MetaLogEngine::close_window(Timestamp end,
                                            std::optional<ReportedWindowBounds> reported_bounds)
{
    if (!window_start_)
        throw std::logic_error{"MetaLogEngine::close_window called before open_window"};

    MetaLogDocument doc;
    stamp_envelope(doc, *window_start_, end, reported_bounds);

    // assert: the queued span edges resolve before the graph is analyzed, so the observed DAG feeds
    // dominant_path and structural_surprise like any other edge.
    // refs: SRC-D-OTEL-11
    resolve_span_edges();

    const WindowAnalysis analysis{analyze_window()};
    build_top_k(doc, analysis);

    std::unordered_set<std::string> reserved;
    build_reservoir(doc, analysis, reserved);
    build_tail_and_entropy(doc, analysis, reserved);

    // assert: presence churn runs after the tail, whose horizon it reads.
    // refs: DN-50.D4
    build_presence_churn(doc);
    build_behavior(doc, analysis);
    build_stability(doc, analysis);
    build_cube(doc);
    // refs: SRC-D-WHERE-4, SRC-D-WHERE-5
    build_acquisition(doc);
    // refs: SRC-D-OTEL-21
    build_service_edges(doc);

    // assert: the n-gram drop count is snapshotted before reset_window_state clears the live
    // counter, which the consumer reads after this returns.
    last_window_ngram_observations_dropped_ = ngram_observations_dropped_;
    stash_prev_window(doc);
    reset_window_state();

    return doc;
}

// post: stamps version, producer and source, the window times and duration, the comparability
// identifiers, and the re-derivation coordinate when one is configured.
void MetaLogEngine::stamp_envelope(MetaLogDocument& doc, Timestamp start, Timestamp end,
                                   std::optional<ReportedWindowBounds> reported_bounds) const
{
    doc.metalog_version = kMetaLogSpecVersion;
    doc.producer.version = config_.producer_version;
    doc.source = source_;

    // note: duration tracks the reported span, not the open/close machinery times.
    // refs: SRC-D-TIR-5
    const Timestamp reported_start{reported_bounds ? reported_bounds->start : start};
    const Timestamp reported_end{reported_bounds ? reported_bounds->end : end};
    doc.window.start_iso = format_rfc3339_utc(reported_start);
    doc.window.end_iso = format_rfc3339_utc(reported_end);

    const auto delta{
        std::chrono::duration_cast<std::chrono::seconds>(reported_end - reported_start).count()};
    doc.window.duration_seconds = delta < 0 ? 0 : static_cast<std::uint64_t>(delta);
    doc.window.lines_observed = lines_observed_;

    // note: the comparability identifiers are opaque contract names stamped from config.
    doc.canonicalization_version = config_.canonicalization_version;
    doc.retention_profile = config_.retention_profile;
    // note: absent for a producer whose config carries no ruleset, so the block is omitted.
    // refs: SRC-II-7
    doc.ruleset = config_.ruleset;
    // invariant: the transport declaration is stamped unconditionally -- an undeclared run carries
    // the block with an empty names[], never a dropped key.
    // refs: ADR-23.D1
    doc.transport = config_.transport;

    // assert: the bounds are integer ticks from the deterministic event timestamps, so they are
    // bit-identical across replays.
    // note: the coordinate is descriptive metadata; no compute below reads it.
    if (config_.source_ref)
    {
        // note: the RAW coordinate carries source_ref and bounds, with children absent.
        ReDerivationCoordinate coord;
        coord.source_ref = *config_.source_ref;
        coord.bounds = EventTimeBounds{
            .start_tick = static_cast<std::uint64_t>(start.time_since_epoch().count()),
            .end_tick = static_cast<std::uint64_t>(end.time_since_epoch().count())};
        coord.canonicalization_version = config_.canonicalization_version;
        doc.coordinate = std::move(coord);
    }
}

// post: a count-sorted bucket view, the transition graph and the per-template surprise band, built
// once before reservoir selection so surprise can feed salience.
MetaLogEngine::WindowAnalysis MetaLogEngine::analyze_window() const
{
    WindowAnalysis analysis;
    // assert: the (count desc, template_id asc) order is a pure function of the contents.
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
    analysis.top_k_cut = std::min(config_.top_k_size, ordered.size());
    build_transition_graph(analysis);
    return analysis;
}

// post: a template's structural_surprise is the surprise of its most-likely incoming transition, so
// a node reached only by a rare edge scores high at severity 0.
// assert: integer-only, and the band depends solely on the winning edge's probability, so
// unordered_map order cannot perturb it.
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
    // note: ratios are compared by cross-multiply, exact integer math.
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
            // assert: on an EQUAL ratio the edge with more observations wins -- a pure function of
            // the contents, never unordered_map order.
            // note: an order-dependent pick would perturb salience and the admission boundary.
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

// post: 0 when the graph is absent or the template has no rare incoming edge.
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

TemplateId MetaLogEngine::template_id_for(const std::string& content_id) const
{
    // pre: content_id was interned at ingest, so the index and the POD both exist.
    return content_templates_by_internal_id_[content_template_index_.find(content_id)->second];
}

void MetaLogEngine::build_top_k(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.top_k_cut;
    const auto total{static_cast<double>(lines_observed_)};

    StatsBlock& stats = doc.stats;
    stats.unique_templates = ordered.size();
    stats.top_k_size = config_.top_k_size;
    stats.top_k.reserve(top_k_cut);

    for (std::size_t i = 0; i < top_k_cut; ++i)
    {
        TopKEntry entry;
        entry.template_id = template_id_for(ordered[i].first);
        // refs: SRC-D-TIR-5
        entry.count = ordered[i].second->count;
        entry.frequency = total > 0.0 ? static_cast<double>(entry.count) / total : 0.0;
        entry.dominant_level = dominant_event_level_of(ordered[i].second->level_counts,
                                                       ordered[i].second->declared_level_counts);
        // invariant: the WHERE label is computed unconditionally -- it is a property of the bucket,
        // so a conditional would make content depend on a consumer's interest.
        // refs: SRC-D-WHERE-2
        if (auto component{dominant_component_of(ordered[i].second->component_counts)};
            !component.empty())
            entry.dominant_component = std::move(component);

        if (config_.max_param_histograms > 0)
        {
            const auto& bucket{*ordered[i].second};
            const auto& content_id{ordered[i].first};
            for (std::size_t pi{0}; pi < bucket.param_value_counts.size(); ++pi)
            {
                FieldHistogram hist;
                hist.param_index = static_cast<std::uint32_t>(pi);
                // assert: no consumer depends on this destination's iteration order.
                const auto& tracked{bucket.param_value_counts[pi]};
                hist.value_counts.reserve(tracked.size());
                hist.value_counts.insert(tracked.begin(), tracked.end());
                hist.total = bucket.param_totals[pi];
                // note: a capped value table makes this entropy an under-estimate.
                std::vector<std::uint64_t> vcounts;
                vcounts.reserve(hist.value_counts.size());
                for (const auto& [value, count] : hist.value_counts)
                    vcounts.push_back(count);
                hist.entropy_bits = shannon_entropy_bits(vcounts, hist.total);
                hist.approximate_cardinality = hll_state_->estimate(content_id, pi);
                entry.field_histograms.push_back(std::move(hist));
            }
        }

        // note: ordinal histograms are emitted in sorted field-name order, so a replay is stable.
        // refs: SRC-D-W1-2
        if (config_.max_param_histograms > 0)
        {
            const auto& bucket{*ordered[i].second};
            std::vector<const std::string*> ordinal_fields;
            ordinal_fields.reserve(bucket.ordinal_accumulators.size());
            for (const auto& [field_name, _accumulator] : bucket.ordinal_accumulators)
                ordinal_fields.push_back(&field_name);
            std::ranges::sort(ordinal_fields, [](const std::string* lhs, const std::string* rhs)
                              { return *lhs < *rhs; });
            for (const std::string* field_name : ordinal_fields)
            {
                const auto& accumulator{bucket.ordinal_accumulators.at(*field_name)};
                OrdinalHistogram hist;
                hist.field_name = *field_name;
                hist.schedule_id = std::string{ordinal_schedule_id(accumulator.schedule)};
                hist.counts = accumulator.counts;
                hist.total = accumulator.total;
                entry.ordinal_histograms.push_back(std::move(hist));
            }
        }

        stats.top_k.push_back(std::move(entry));
    }
}

void MetaLogEngine::build_reservoir(MetaLogDocument& doc, const WindowAnalysis& analysis,
                                    std::unordered_set<std::string>& reserved) const
{
    // invariant: the reservoir is disjoint from top_k by construction and its admits are excluded
    // from the tail residual, so nothing is double-counted.
    if (config_.reservoir_size == 0 || analysis.ordered.size() <= analysis.top_k_cut)
        return;
    auto candidates = collect_reservoir_candidates(analysis);
    admit_reservoir(doc.stats, analysis, candidates, reserved);
}

// post: the below-top_k templates with a non-zero integer salience.
std::vector<MetaLogEngine::ReservoirCandidate>
MetaLogEngine::collect_reservoir_candidates(const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.top_k_cut;
    std::vector<ReservoirCandidate> candidates;
    for (std::size_t i = top_k_cut; i < ordered.size(); ++i)
    {
        const Bucket& bucket{*ordered[i].second};
        const auto surprise{surprise_of(analysis, ordered[i].first)};
        const auto novelty{novelty_band(bucket.first_seen_index, lines_observed_, bucket.count)};
        const auto sal{salience_score(dominant_level_of(bucket.level_counts),
                                      dominant_role_of(bucket.role_counts), bucket.template_str,
                                      bucket.all_echoed_source, bucket.count, lines_observed_,
                                      surprise, novelty)};
        if (sal.score > 0U)
            candidates.push_back(ReservoirCandidate{.index = i,
                                                    .salience = sal.score,
                                                    .structural_surprise = surprise,
                                                    .novelty = novelty,
                                                    .retention_axis = sal.axis});
    }
    return candidates;
}

// refs: SRC-D-RNK-2
// note: NOLINT: one coherent determinism-critical admission routine, pinned by its test.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MetaLogEngine::admit_reservoir(StatsBlock& stats, const WindowAnalysis& analysis,
                                    std::vector<ReservoirCandidate>& candidates,
                                    std::unordered_set<std::string>& reserved) const
{
    const auto& ordered = analysis.ordered;
    const auto total{static_cast<double>(lines_observed_)};
    // invariant: admission is salience-ranked with a tie-break by template_id, so a given input
    // under a matching retention_profile yields a bit-identical reservoir.
    std::ranges::sort(candidates,
                      [&ordered](const ReservoirCandidate& lhs, const ReservoirCandidate& rhs)
                      {
                          if (lhs.salience != rhs.salience)
                              return lhs.salience > rhs.salience;
                          return ordered[lhs.index].first < ordered[rhs.index].first;
                      });

    // note: the level half reuses the exported is_failure_level rather than a fourth copy.
    // refs: SRC-D-RNK-2, SRC-D-OUT-4, DN-64.D3
    const auto error_class{
        [](StructuralRole role, const std::optional<EventLevel>& level) noexcept
        { return role == StructuralRole::Terminator || is_failure_level(level); }};
    // note: the kind key packs the two enums into one counter, so the cap buys coverage.
    constexpr unsigned kKindRoleShift{8U};
    const auto kind_key{[](StructuralRole role, std::optional<EventLevel> level) noexcept
                        {
                            const auto lvl{level ? static_cast<std::uint16_t>(level->value())
                                                 : std::uint16_t{0xFFU}};
                            return static_cast<std::uint16_t>(
                                (static_cast<std::uint16_t>(role) << kKindRoleShift) | lvl);
                        }};
    // post: builds and pushes one entry and marks it reserved, out of the tail residual.
    const auto admit_one{
        [&](const ReservoirCandidate& candidate, std::optional<EventLevel> level,
            StructuralRole role)
        {
            const Bucket& bucket{*ordered[candidate.index].second};
            ReservoirEntry entry;
            entry.template_id = template_id_for(ordered[candidate.index].first);
            // refs: SRC-D-TIR-5
            entry.count = bucket.count;
            entry.frequency = total > 0.0 ? static_cast<double>(bucket.count) / total : 0.0;
            entry.dominant_level = level;
            entry.structural_role = role;
            entry.structural_surprise = candidate.structural_surprise;
            entry.novelty = candidate.novelty;
            entry.salience = candidate.salience;
            entry.retention_axis = candidate.retention_axis;
            // note: the sub-coordinate is stamped only when a document coordinate is configured.
            if (config_.source_ref)
                entry.within_window_ordinal = bucket.first_seen_index;
            // note: an empty component gives a disengaged label and the aggregated-WHERE star.
            // refs: SRC-D-WHERE-2
            {
                auto component{dominant_component_of(bucket.component_counts)};
                entry.cube_coord = cube::cube_location(
                    level ? std::optional<LogLevel>{level->value()} : std::nullopt, component);
                if (!component.empty())
                    entry.dominant_component = std::move(component);
            }
            stats.reservoir.push_back(std::move(entry));
            reserved.insert(ordered[candidate.index].first);
        }};

    // invariant: the cap is declared at the site that enforces it -- both phases stop at
    // config_.reservoir_size, so declaration and bound cannot drift apart.
    stats.reservoir_size = config_.reservoir_size;
    stats.reservoir.reserve(std::min(config_.reservoir_size, candidates.size()));

    // invariant: the error-class reserve is exempt from the per-kind cap and runs before the
    // general pool, so benign salience cannot crowd out a real failure.
    // note: the reserve buys failure DEPTH; a storm overflows it and the top by salience win.
    // refs: SRC-D-RNK-2
    const auto reserve{std::min(config_.reservoir_error_reserve, config_.reservoir_size)};
    if (reserve > 0)
    {
        std::size_t reserved_used{0};
        for (const auto& candidate : candidates)
        {
            if (reserved_used >= reserve || stats.reservoir.size() >= config_.reservoir_size)
                break;
            const Bucket& bucket{*ordered[candidate.index].second};
            const auto level{
                dominant_event_level_of(bucket.level_counts, bucket.declared_level_counts)};
            const auto role{dominant_role_of(bucket.role_counts)};
            if (!error_class(role, level))
                continue;
            admit_one(candidate, level, role);
            ++reserved_used;
        }
    }

    // invariant: the per-kind counters start fresh -- the reserve is a separate, exempt budget.
    std::unordered_map<std::uint16_t, std::size_t> per_kind;
    for (const auto& candidate : candidates)
    {
        if (stats.reservoir.size() >= config_.reservoir_size)
            break;
        if (reserved.contains(ordered[candidate.index].first))
            continue;
        const Bucket& bucket{*ordered[candidate.index].second};
        const auto level{
            dominant_event_level_of(bucket.level_counts, bucket.declared_level_counts)};
        const auto role{dominant_role_of(bucket.role_counts)};
        if (config_.reservoir_per_kind_cap > 0)
        {
            auto& kind_count{per_kind[kind_key(role, level)]};
            if (kind_count >= config_.reservoir_per_kind_cap)
                continue;
            ++kind_count;
        }
        admit_one(candidate, level, role);
    }
}

void MetaLogEngine::build_tail_and_entropy(MetaLogDocument& doc, const WindowAnalysis& analysis,
                                           const std::unordered_set<std::string>& reserved) const
{
    const auto& ordered = analysis.ordered;
    const auto top_k_cut = analysis.top_k_cut;
    StatsBlock& stats = doc.stats;
    std::uint64_t tail_count = 0;
    std::uint64_t tail_max = 0;
    std::vector<std::uint64_t> tail_counts;
    if (ordered.size() > top_k_cut)
        tail_counts.reserve(ordered.size() - top_k_cut);
    for (std::size_t i = top_k_cut; i < ordered.size(); ++i)
    {
        if (reserved.contains(ordered[i].first))
            continue;
        const auto count = ordered[i].second->count;
        tail_count += count;
        // note: NOLINT: a defensive clamp on the hot path, not a min/max of two operands.
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (count > tail_max)
            tail_max = count;
        tail_counts.push_back(count);
    }
    stats.tail_count = tail_count;
    stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

    // post: emitted when the tail holds at least one template; entropy is normalised over the tail
    // mass, never over lines_observed_.
    if (stats.tail_unique > 0 && lines_observed_ > 0)
    {
        TailSummary summary;
        summary.tail_template_count = stats.tail_unique;
        summary.tail_entropy_bits = shannon_entropy_bits(tail_counts, tail_count);
        summary.tail_max_rate =
            static_cast<double>(tail_max) / static_cast<double>(lines_observed_);
        stats.tail_summary = summary;
    }

    // note: entropy_bits covers the full untruncated template distribution.
    if (lines_observed_ > 0)
    {
        std::vector<std::uint64_t> counts;
        counts.reserve(ordered.size());
        for (const auto& entry : ordered)
            counts.push_back(entry.second->count);
        stats.entropy_bits = shannon_entropy_bits(counts, lines_observed_);
    }
}

// post: every retained template gets the base element -- span 1, no transitions, no indeterminate,
// Present, Present.
// note: an event-free window stamps nothing, so silence reads as continuous presence.
// refs: DN-50.D4
void MetaLogEngine::build_presence_churn(MetaLogDocument& doc) const
{
    if (doc.window.lines_observed == 0)
        return;

    const PresenceChurn retained{presence_churn_of_retained_window()};
    for (TopKEntry& entry : doc.stats.top_k)
        entry.presence_churn = retained;
    for (ReservoirEntry& entry : doc.stats.reservoir)
        entry.presence_churn = retained;
    doc.presence_churn = PresenceChurnSummary{.span_windows = 1,
                                              .templates_with_churn = 0,
                                              .total_transitions = 0,
                                              .total_indeterminate = 0};
}

void MetaLogEngine::build_behavior(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    if (config_.top_ngrams_size == 0 || ngram_total_ == 0)
        return;

    BehaviorBlock behavior;
    behavior.ngram_size = config_.ngram_size;
    behavior.top_ngrams_size = config_.top_ngrams_size;
    // invariant: dropped_ngram_observations is declared at the window whose bound it describes and
    // OMITTED when that bound refused nothing.
    // assert: read before close_window's snapshot, so the live counter is still this one's.
    // refs: ADR-9.D3
    if (ngram_observations_dropped_ > 0)
        behavior.dropped_ngram_observations = ngram_observations_dropped_;
    build_top_ngrams(behavior);

    // note: the edge count is read from the transition view built in analyze_window.
    std::uint64_t edge_count = 0;
    for (const auto& [from, row] : analysis.transitions)
        edge_count += row.size();
    behavior.graph_edge_count = edge_count;

    build_branching(behavior, analysis);
    build_dominant_path(behavior, analysis);
    doc.behavior = std::move(behavior);
}

// post: the highest-count n-grams with p(last | prefix).
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

// post: per-source fanout and outgoing-transition entropy.
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
            // assert: the branching entropy is accumulated in the exact integer/count domain.
            insight::det::FixedReducer reducer;
            const std::int64_t log2_total{insight::det::det_log2_fixed(total)};
            for (const auto& [_sinked, count] : row)
            {
                if (count == 0)
                    continue;
                // note: the u64 count widens value-preserving via u128, matching native widening.
                reducer.add_fixed(static_cast<insight::det::i128>(insight::det::u128{count}) *
                                  (log2_total - insight::det::det_log2_fixed(count)));
            }
            entry.entropy_bits = reducer.normalized_bits(static_cast<std::int64_t>(total));
        }
        branching_rows.push_back(entry);
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
    // invariant: an omitted branching_size asserts no cap; this producer caps, so the declaration
    // is owed and is made here, at the truncation that enforces it.
    behavior.branching_size = config_.top_branching_size;
}

// post: a greedy highest-count walk from the busiest template.
void MetaLogEngine::build_dominant_path(BehaviorBlock& behavior,
                                        const WindowAnalysis& analysis) const
{
    if (config_.dominant_path_max_steps == 0 || buckets_.empty())
        return;
    const auto& transitions = analysis.transitions;

    std::vector<TemplateId> path;
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

// post: the highest-count template, ties broken toward the lower internal id.
MetaLogEngine::InternalTemplateID MetaLogEngine::dominant_path_start() const
{
    InternalTemplateID start_id{0};
    std::uint64_t best_count{0};
    // assert: the (count desc, internal-id asc) tie-break makes the result independent of the
    // bucket map's iteration order.
    for (const auto& [content_id, bucket] : buckets_)
    {
        const InternalTemplateID tid{content_template_index_.find(content_id)->second};
        if (bucket.count > best_count || (bucket.count == best_count && tid < start_id))
        {
            best_count = bucket.count;
            start_id = tid;
        }
    }
    return start_id;
}

void MetaLogEngine::build_stability(MetaLogDocument& doc, const WindowAnalysis& analysis) const
{
    const auto& ordered = analysis.ordered;
    // post: emitted from the second window onwards; stability_score is 1 - js_divergence in [0, 1]
    // with log2 JS.
    if (config_.emit_stability && prev_window_end_iso_ && prev_total_ > 0 && lines_observed_ > 0)
    {
        std::unordered_map<TemplateId, std::uint64_t> cur_freq;
        cur_freq.reserve(ordered.size());
        for (const auto& [content_id, bucket] : buckets_)
            cur_freq.emplace(template_id_for(content_id), bucket.count);

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
        // note: NOLINT: the lower half of a defensive clamp to [0, 1], not a min/max.
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (stability.stability_score < 0.0)
            stability.stability_score = 0.0;
        // note: NOLINT: the upper half of the same defensive clamp.
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (stability.stability_score > 1.0)
            stability.stability_score = 1.0;
        doc.stability = std::move(stability);
    }
}

// post: the per-event joint is flattened into BaseRows and closed in batch over the frozen window,
// so the cube is a pure function of that set.
void MetaLogEngine::build_cube(MetaLogDocument& doc) const
{
    std::vector<cube::BaseRow> base;
    base.reserve(cube_base_.size());
    for (const auto& [key, count] : cube_base_)
    {
        const auto& [level, component, role]{key};
        base.push_back(
            cube::BaseRow{.level = level, .component = component, .role = role, .count = count});
    }
    doc.cube = cube::build_closed_cube(base);
    doc.has_cube = true;
}

// post: records_with_component is the total located events and distinct_components the size of the
// union of component values; both are order-independent.
// note: the producer states the facts; the consumer applies the coverage predicate.
// refs: SRC-D-WHERE-4, SRC-D-WHERE-5
void MetaLogEngine::build_acquisition(MetaLogDocument& doc) const
{
    AcquisitionBlock acquisition;
    std::unordered_set<std::string> distinct;
    for (const auto& [content_id, bucket] : buckets_)
        for (const auto& [component, count] : bucket.component_counts)
        {
            acquisition.records_with_component += count;
            distinct.insert(component);
        }
    acquisition.distinct_components = distinct.size();

    // note: the WHERE axis is one depth-1 chain today, so the vector holds one entry.
    acquisition.where_cardinality_per_depth.push_back(acquisition.distinct_components);

    // pre: build_cube ran first, so the closed cube is available to read.
    // note: WHERE cardinality equals distinct_components above and is not re-stored.
    const CubeCardinalityStat card{cube_cardinality(doc.cube)};
    acquisition.closed_cells = card.cells;
    acquisition.level_cardinality = card.per_axis[static_cast<std::size_t>(CardinalityAxis::Level)];
    acquisition.role_cardinality = card.per_axis[static_cast<std::size_t>(CardinalityAxis::Role)];

    // note: the span facts are raw integer counts, threshold-free.
    // refs: SRC-D-OTEL-13, SRC-D-OTEL-11, SRC-D-OTEL-9
    acquisition.span_records = span_records_;
    acquisition.orphan_parent_edges = orphan_parent_edges_;
    acquisition.orphan_link_edges = orphan_link_edges_;

    doc.acquisition = acquisition;
}

// post: emitted iff the window had trace substrate; a present-but-empty block says "no topology",
// never "unknown".
// note: dropped_edges is the honest truncation count.
// refs: SRC-D-OTEL-21
void MetaLogEngine::build_service_edges(MetaLogDocument& doc) const
{
    if (span_records_ == 0)
        // refs: SRC-D-OTEL-20
        return;

    ServiceEdgeBlock block;
    if (service_edges_.size() <= config_.max_service_edges)
    {
        block.edges.reserve(service_edges_.size());
        for (const auto& [pair, weight] : service_edges_)
            block.edges.push_back(
                ServiceEdge{.caller = pair.first, .callee = pair.second, .weight = weight});
    }
    else
    {
        // note: selection runs on a view ordered by weight desc then canonical key asc.
        std::vector<std::pair<std::pair<std::string, std::string>, std::uint64_t>> view(
            service_edges_.begin(), service_edges_.end());
        std::ranges::sort(view,
                          [](const auto& lhs, const auto& rhs)
                          {
                              if (lhs.second != rhs.second)
                                  return lhs.second > rhs.second;
                              return lhs.first < rhs.first;
                          });
        block.dropped_edges = service_edges_.size() - config_.max_service_edges;
        view.resize(config_.max_service_edges);
        block.edges.reserve(view.size());
        for (const auto& [pair, weight] : view)
            block.edges.push_back(
                ServiceEdge{.caller = pair.first, .callee = pair.second, .weight = weight});
        std::ranges::sort(
            block.edges, [](const ServiceEdge& lhs, const ServiceEdge& rhs)
            { return std::tie(lhs.caller, lhs.callee) < std::tie(rhs.caller, rhs.callee); });
    }
    doc.service_edges = std::move(block);
}

void MetaLogEngine::stash_prev_window(const MetaLogDocument& doc)
{
    // note: this window's frequency map is stashed for the next window's stability.
    if (config_.emit_stability)
    {
        prev_freq_.clear();
        prev_freq_.reserve(buckets_.size());
        for (const auto& [content_id, bucket] : buckets_)
            prev_freq_.emplace(template_id_for(content_id), bucket.count);
        prev_total_ = lines_observed_;
        prev_window_end_iso_ = doc.window.end_iso;
    }
}

void MetaLogEngine::reset_window_state()
{
    window_start_.reset();
    lines_observed_ = 0;
    buckets_.clear();
    template_str_cache_.clear();
    content_template_index_.clear();
    content_templates_by_internal_id_.clear();
    global_ring_ = {};
    trace_rings_.clear();
    trace_ring_fifo_.clear();
    // refs: SRC-D-OTEL-11
    span_templates_.clear();
    span_fifo_.clear();
    pending_span_edges_.clear();
    span_records_ = 0;
    orphan_parent_edges_ = 0;
    orphan_link_edges_ = 0;
    // note: the per-window span, link and service-topology state all reset together.
    // refs: SRC-D-OTEL-9, SRC-D-OTEL-21
    pending_link_edges_.clear();
    service_edges_.clear();
    ngram_counts_.clear();
    ngram_total_ = 0;
    ngram_observations_dropped_ = 0;
    cube_base_.clear();
}

} // namespace insight::metalog
