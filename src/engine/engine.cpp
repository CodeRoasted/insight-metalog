module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.cube;

// MetaLog producer engine. The stateful streaming side: one window
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
    template_str_cache_.clear();
    content_template_index_.clear();
    content_templates_by_internal_id_.clear();
    global_ring_ = {};
    trace_rings_.clear();
    trace_ring_fifo_.clear();
    // SRC-D-OTEL-11: per-window span state resets with the trace state
    span_templates_.clear();
    span_fifo_.clear();
    pending_span_edges_.clear();
    span_records_ = 0;
    orphan_parent_edges_ = 0;
    ngram_counts_.clear();
    ngram_total_ = 0;
    ngram_observations_dropped_ = 0; // per-window like the table it guards (ADR-9.D3)
    cube_base_.clear();
    (*hll_state_).reset();
    // NOTE: prev_freq_ / prev_window_end_iso_ are NOT cleared here —
    // they are the cross-window state that feeds the stability block.
}

MetaLogEngine::TemplateLookup
MetaLogEngine::content_template_id_for(const tokenization::CanonicalEvent& event)
{
    // Fast path: template_str is the content-deterministic identity (stateless masker),
    // so it maps to exactly ONE content_id — a hit returns without recomputing the SHA.
    if (auto cached{template_str_cache_.find(event.template_str)};
        cached != template_str_cache_.end())
    {
        return {.content_id = &cached->second.content_id,
                .internal_id = cached->second.internal_id};
    }

    // Compute the canon TemplateId POD once; render the "h:"+hex string for the engine's
    // string-keyed per-window state (buckets_/index/cache). The POD is what the domain
    // carries (SRC-D-TIR-2); the string stays the engine-internal key.
    const TemplateId template_id{insight::template_id_of(event.template_str)};
    std::string content_id{insight::render(template_id)};

    auto index_it{content_template_index_.find(content_id)};
    InternalTemplateID internal_id{};
    if (index_it == content_template_index_.end())
    {
        internal_id = static_cast<InternalTemplateID>(content_templates_by_internal_id_.size());
        content_templates_by_internal_id_.push_back(template_id);
        content_template_index_.emplace(content_id, internal_id);
        // SRC-D-TIR-5: the engine's registry is the single home of the display-only template_str,
        // interned once per unique id and resolved at the serialize/explain seams. Accumulates
        // across windows (the vocabulary), so older docs/baselines resolve by id.
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
            // Bounded: refuse new keys past the cap. The refusal is COUNTED — before this, the
            // `return` sat ahead of `++ngram_total_`, so neither the key nor the observation was
            // recorded and a consumer could not tell a complete n-gram distribution from a
            // truncated one (ADR-9.D3: lossiness is always VISIBLE). One increment on a branch
            // that already existed; the WARN fires once per window in the eidos pipeline.
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
    // Bigram needs >=1 prior id in the ring; trigram needs >=2. (Identical formation to the
    // pre-OTEL global-ring path — only the ring it reads changes, so non-OTEL output is
    // byte-identical and OTEL output is trace-scoped.)
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

    // Shift ring: [1] = old [0]; [0] = internal_id.
    ring.recent[1] = ring.recent[0];
    ring.recent[0] = internal_id;
    if (ring.filled < 2)
        ++ring.filled;
}

MetaLogEngine::NgramRing& MetaLogEngine::trace_ring_for(TraceId trace_id)
{
    if (auto iterator{trace_rings_.find(trace_id)}; iterator != trace_rings_.end())
        return iterator->second;
    // First sight of this trace. Enforce the active-trace bound (OR3) with deterministic FIFO
    // eviction of the oldest-inserted trace (front of the queue). A ring is just 2 ids, so
    // eviction costs at most one cross-record edge for the evicted trace, never its membership.
    // max_active_traces == 0 disables the bound (caller's explicit choice).
    if (config_.max_active_traces > 0 && trace_rings_.size() >= config_.max_active_traces &&
        !trace_ring_fifo_.empty())
    {
        trace_rings_.erase(trace_ring_fifo_.front());
        trace_ring_fifo_.pop_front();
    }
    trace_ring_fifo_.push_back(trace_id);
    return trace_rings_[trace_id]; // value-initialised NgramRing (filled = 0)
}

void MetaLogEngine::record_span(const tokenization::CanonicalEvent& event,
                                InternalTemplateID internal_id)
{
    ++span_records_; // the SRC-D-OTEL-13 licence fact (span vocabulary is spoken iff span_records_
                     // > 0)
    const SpanId span_id{event.trace.span_id};
    // Remember span_id → template for close-time parent resolution. Bounded FIFO under
    // max_active_spans with deterministic eviction of the oldest-inserted span (the O2 discipline).
    // A span id is unique per (trace, span); on a rare hash collision keep the first
    // (insert-if-new).
    if (!span_templates_.contains(span_id))
    {
        if (config_.max_active_spans > 0 && span_templates_.size() >= config_.max_active_spans &&
            !span_fifo_.empty())
        {
            span_templates_.erase(span_fifo_.front());
            span_fifo_.pop_front();
        }
        // Remember the template id (observed template edge) + the component (O4b service edge,
        // SRC-D-OTEL-21).
        span_templates_.emplace(span_id, SpanNode{.template_id = internal_id,
                                                  .component = std::string{event.component}});
        span_fifo_.push_back(span_id);
    }
    // Queue the declared parent edge (child template + component known now; parent resolved at
    // close, since a child routinely serializes before its parent). In ingest order →
    // deterministic.
    if (event.trace.has_parent)
        pending_span_edges_.push_back({.child_template = internal_id,
                                       .parent_span_id = event.trace.parent_span_id,
                                       .child_component = std::string{event.component}});

    // O4b Span Links (SRC-D-OTEL-9): queue each declared cross-trace edge source_component → linked
    // span. The linked span (and its component) resolves at close, by span_id, across traces.
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
            ++orphan_parent_edges_; // parent evicted / straddled the window — counted, never
                                    // guessed
            continue;
        }
        // The OBSERVED causal edge template(parent) → template(child) as a bigram in the SAME
        // bounded graph the inferred path feeds (one fingerprint, no fork — O2). It maps to
        // transitions[parent][child] exactly like an inferred bigram, so dominant_path /
        // structural_surprise consume it transparently. Counts are commutative → order-independent.
        NGramKey key{.size = 2};
        key.ids[0] = parent_it->second.template_id;
        key.ids[1] = edge.child_template;
        account_ngram(key);

        // O4b (SRC-D-OTEL-21): DISTIL the same declared edge to component granularity → the service
        // topology. Excluded: an unknown endpoint (empty service.name — cannot be a topology node)
        // and a self-edge (same-component parentage is intra-service, not topology). Integer
        // weight, keyed in canonical (caller, callee) order (std::map) → deterministic, no float.
        const std::string& caller{parent_it->second.component};
        const std::string& callee{edge.child_component};
        if (!caller.empty() && !callee.empty() && caller != callee)
            ++service_edges_[{caller, callee}];
    }

    // O4b Span Links (SRC-D-OTEL-9): resolve each declared cross-trace edge into the SAME service
    // topology — source_component → component(linked span). Resolution is by span_id (across
    // traces); an unresolved link (target outside the window / evicted) yields no edge, counted as
    // an orphan (the declared-not-guessed discipline). Self-edges + unknown endpoints excluded,
    // like intra-trace edges.
    for (const auto& link : pending_link_edges_)
    {
        const auto target_it{span_templates_.find(link.linked_span_id)};
        if (target_it == span_templates_.end())
        {
            ++orphan_link_edges_; // target span not in this window (cross-route / external) —
                                  // counted, never guessed; SIBLING to orphan_parent_edges
                                  // (SRC-D-OTEL-9, Founder ruling)
            continue;
        }
        const std::string& caller{link.source_component};
        const std::string& callee{target_it->second.component};
        if (!caller.empty() && !callee.empty() && caller != callee)
            ++service_edges_[{caller, callee}];
    }
}

// hot-path per-event accumulator — one coherent routine folding an event into
// template/cube/component/param-histogram/HLL state; a split fragments the hot path and its
// locality.
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
        bucket.first_seen_index = lines_observed_; // ordinal of this first occurrence
    }
    ++bucket.count;
    ++bucket.level_counts[event.level];
    // DN-32.D3: the same observation, counted a second time only when its level was DECLARED.
    // The pair is written here together, off one event, so a level can never be accumulated
    // without its provenance being accounted for.
    if (event.declared_level)
        ++bucket.declared_level_counts[event.level];
    ++bucket.role_counts[event.structural_role]; // announced role → salience
    // SRC-D-PROV-1 (§3.1): a template is "all echoed" only while every event forming it is echoed
    // source. AND-reduction (order-independent) — one real runtime occurrence makes it false.
    bucket.all_echoed_source = bucket.all_echoed_source && event.echoed_source;
    ++lines_observed_;

    // SPEC §16 cube: accumulate the per-EVENT joint (level, component, role) — ALWAYS
    // (the cube is unconditional; the collapse guardrail bounds its cardinality, §C). The
    // component string_view is arena-stable only within the window, so it is copied into
    // the key ON FIRST SIGHT ONLY: a steady-state hit looks up by string_view through the
    // transparent comparator and constructs nothing (ADR-9.D2 — measured at 1 heap
    // allocation per event per >SSO component before this, bench_cube_key_alloc).
    if (auto hit{
            cube_base_.find(std::make_tuple(event.level, event.component, event.structural_role))};
        hit != cube_base_.end())
        ++hit->second;
    else
        ++cube_base_[std::make_tuple(event.level, std::string{event.component},
                                     event.structural_role)];

    // Per-template component marginal — the WHERE carrier (SRC-D-WHERE-2) and the §16.6
    // reservoir cross, feeding both the cube and the leaf `dominant_component`. Always
    // accumulated: the emit_cube/emit_where opt-in gates that once governed this population were
    // removed (metalog.api.cppm), so no population predicate survives to state here. Empty
    // components are not counted (records_with_component then counts only located records).
    if (!event.component.empty())
    {
        // Same shape as the cube key above: hit by view, copy on first sight only.
        if (auto hit{bucket.component_counts.find(event.component)};
            hit != bucket.component_counts.end())
            ++hit->second;
        else
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

    // W1 ordinal observations (§4A.4 SRC-D-W1-2): bin each declared ordinal value (canon
    // kOrdinalFieldCatalog) onto its schedule's log2 ladder. Same batch / full-fidelity gate as
    // param histograms; field-keyed (not positional) — never collides with param_value_counts.
    if (config_.max_param_histograms > 0 && !event.ordinals.empty())
    {
        for (const auto& observation : event.ordinals)
        {
            auto [ord_it, ord_inserted]{
                bucket.ordinal_accumulators.try_emplace(std::string{observation.field_name})};
            auto& accumulator{ord_it->second};
            if (ord_inserted)
            {
                accumulator.schedule = observation.schedule;
                accumulator.counts.assign(ordinal_schedule_bins(observation.schedule), 0U);
            }
            const std::uint32_t bin{ordinal_bin_index(observation.schedule, observation.value)};
            if (bin < accumulator.counts.size())
            {
                ++accumulator.counts[bin];
                ++accumulator.total;
            }
        }
    }

    // n-gram update (ADR-29.D1 — the trace-scoped graph). An OTEL event forms its
    // n-gram WITHIN its trace (the per-trace ring), so a bigram/trigram is "B followed A inside
    // ONE transaction", not across the global concurrent interleave — de-polluting dominant_path
    // / structural_surprise under concurrency. A non-OTEL event uses the single global ring (the
    // pre-OTEL path, byte-identical). Both rings feed the SAME bounded ngram_counts_ graph
    // (one fingerprint, no fork — O2): the per-trace n-grams aggregate into the global graph,
    // never a per-trace sub-fingerprint (OR3).
    // SRC-D-OTEL-11: a SPAN record's causality is DECLARED — record its span_id → template and
    // queue its parent edge for close-time resolution; it NEVER enters an adjacency ring. Log
    // records (with or without trace context) keep the O2 ring path above/below.
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

    // SRC-D-OTEL-11: resolve the window's queued span parent edges into ngram_counts_ BEFORE
    // the graph is analyzed, so the observed DAG feeds dominant_path / structural_surprise like any
    // other edge. No-op for a non-span window (pending_span_edges_ empty).
    resolve_span_edges();

    const WindowAnalysis analysis{analyze_window()};
    build_top_k(doc, analysis);

    std::unordered_set<std::string> reserved; // content_ids promoted to the reservoir
    build_reservoir(doc, analysis, reserved);
    build_tail_and_entropy(doc, analysis, reserved);

    build_behavior(doc, analysis);
    build_stability(doc, analysis);
    build_cube(doc); // SPEC §16 — always (unconditional; collapse-bounded, §C)
    // SRC-D-WHERE-4/SRC-D-WHERE-5 — always (the window's dimension self-assessment)
    build_acquisition(doc);
    build_service_edges(
        doc); // O4b (SRC-D-OTEL-21) — iff the window had trace substrate (span_records > 0)

    // Carry this window's frequencies for the next window's stability, then drop the
    // per-window state. The n-gram drop count is snapshotted FIRST: reset_window_state() clears
    // the live counter, and the consumer reads the snapshot after this function returns.
    last_window_ngram_observations_dropped_ = ngram_observations_dropped_;
    stash_prev_window(doc);
    reset_window_state();

    return doc;
}

// Stamp the envelope: version/producer/source, window times + duration, the §2.4
// processing identifiers, and the §15 re-derivation coordinate (when configured).
void MetaLogEngine::stamp_envelope(MetaLogDocument& doc, Timestamp start, Timestamp end,
                                   std::optional<ReportedWindowBounds> reported_bounds) const
{
    doc.metalog_version = kMetaLogSpecVersion;
    doc.producer.version = config_.producer_version;
    doc.source = source_;
    // SRC-D-TIR-5: template strings are not carried on the doc — the serialiser resolves them by id
    // from the engine registry at the serialize/explain seams (SPEC §3.4 inline).

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
    // SRC-II-7 composed-ruleset identity (ADR-17): the semantic_identity + package list of the
    // ruleset that segmented the input, injected via config by the producing binary. Absent for a
    // legacy producer (config.ruleset unset) → the block is omitted from the wire.
    doc.ruleset = config_.ruleset;
    // ADR-23 per-run transport declaration: stamped UNCONDITIONALLY, so a produced document always
    // states what was declared — an undeclared run carries the block with an empty `names[]`
    // rather than dropping it, because a key emitted only for a non-empty stack is
    // indistinguishable from a producer that cannot emit the key at all.
    doc.transport = config_.transport;

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
    analysis.top_k_cut = std::min(config_.top_k_size, ordered.size());
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
            // integer). DETERMINISTIC tie-break on EQUAL ratio: prefer the edge with MORE
            // observations (larger count) — a pure function of the contents, NOT the unordered_map
            // iteration order. Two equal-ratio edges with different absolute (count, outgoing) can
            // fall in DIFFERENT surprise bands (the ≥2-observation floor + the integer thresholds
            // in surprise_band), so an order-dependent pick diverges across stdlibs and perturbs
            // structural_surprise → the salience ranking → the reservoir admission boundary. (Found
            // via the §9.2 cross-count clang≢gcc; the same determinism discipline as
            // dominant_level_of/dominant_role_of.)
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

TemplateId MetaLogEngine::template_id_for(const std::string& content_id) const
{
    // content_id was interned at ingest, so the index + the by-internal-id POD both exist
    // — find()->second over .at() skips the bounds-check/throw branch (no exceptions on
    // the per-template window-build path; const method, so operator[] is unavailable).
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
        // SRC-D-TIR-5 field-drop: the display template_str is no longer copied onto the entry — it
        // lives in the engine registry (interned at ingest), resolved by id at the
        // serialize/explain seams.
        entry.count = ordered[i].second->count;
        entry.frequency = total > 0.0 ? static_cast<double>(entry.count) / total : 0.0;
        entry.dominant_level = dominant_event_level_of(ordered[i].second->level_counts,
                                                       ordered[i].second->declared_level_counts);
        // SRC-D-WHERE-2 — see metalog.api.cppm (TopKEntry) for the contract. Computed
        // unconditionally here: the label is a property of the bucket, so making it
        // conditional would make the document's content depend on a consumer's interest.
        if (auto component{dominant_component_of(ordered[i].second->component_counts)};
            !component.empty())
            entry.dominant_component = std::move(component);

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

        // W1 ordinal histograms (§4A.4 SRC-D-W1-2) — field-keyed, emitted in deterministic
        // field-name order (the accumulator map is unordered → sort to keep the wire/golden
        // replay-stable).
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
    // ── Tier 2: Salience Reservoir ──
    // From the below-top_k templates, retain the most SALIENT (not the most
    // frequent) — this is where a rare-but-severe event (a lone fatal) survives
    // instead of collapsing into the tail. Disjoint from top_k by construction
    // (candidates are ordered[k..]); admitted templates are excluded from the tail
    // residual so they are not double-counted.
    if (config_.reservoir_size == 0 || analysis.ordered.size() <= analysis.top_k_cut)
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

// Admit candidates to the reservoir in salience order (SPEC §3.7.2 MUST: tie-break
// by template_id for a bit-identical reservoir), bounded by reservoir_size, with a
// guaranteed error-class RESERVE (SRC-D-RNK-2 §5.2) ahead of the per-kind
// (structural_role × dominant_level) diversity cap on the general pool.
// SPEC §3.7.2 bit-identical salience-ranked reservoir admission — one coherent
// determinism-critical routine (tie-break by template_id), pinned by
// ReservoirTest.TieBreakByTemplateIdAtEqualSalience.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

    // The error class (SRC-D-RNK-2 §5.2) — mirrors eidos `reservoir_is_error_class`: the
    // verdict-anchored-failure signal at the metalog layer (after SRC-D-OUT-4). The LEVEL half is
    // `is_failure_level`, the exported one spelling (DN-64.D3 row 6) rather than a fourth copy of
    // the `Unknown sorts above Fatal` membership test; the Terminator disjunct is this class's own.
    const auto error_class{
        [](StructuralRole role, const std::optional<EventLevel>& level) noexcept
        { return role == StructuralRole::Terminator || is_failure_level(level); }};
    // Per-kind diversity key for the general pool: packs the two small enums into one
    // integer counter so M optimises COVERAGE of distinct salient kinds over depth.
    constexpr unsigned kKindRoleShift{8U};
    const auto kind_key{[](StructuralRole role, std::optional<EventLevel> level) noexcept
                        {
                            const auto lvl{level ? static_cast<std::uint16_t>(level->value())
                                                 : std::uint16_t{0xFFU}};
                            return static_cast<std::uint16_t>(
                                (static_cast<std::uint16_t>(role) << kKindRoleShift) | lvl);
                        }};
    // Build + push one reservoir entry for an already-decided candidate; mark it reserved
    // (excluded from the tail residual, SPEC §3.7.3). level/role are passed in so the two
    // admission phases compute the dominant maps once.
    const auto admit_one{
        [&](const ReservoirCandidate& candidate, std::optional<EventLevel> level,
            StructuralRole role)
        {
            const Bucket& bucket{*ordered[candidate.index].second};
            ReservoirEntry entry;
            entry.template_id = template_id_for(ordered[candidate.index].first);
            // SRC-D-TIR-5 field-drop: template_str resolved by id from the registry at display
            // seams.
            entry.count = bucket.count;
            entry.frequency = total > 0.0 ? static_cast<double>(bucket.count) / total : 0.0;
            entry.dominant_level = level;
            entry.structural_role = role;
            entry.structural_surprise = candidate.structural_surprise;
            entry.novelty = candidate.novelty;
            entry.salience = candidate.salience;
            entry.retention_axis = candidate.retention_axis;
            // §15.4 sub-coordinate: re-express the reconciled first-seen ordinal, bounded by M.
            // Only when a coordinate is configured (a sub-part of the document coordinate).
            if (config_.source_ref)
                entry.within_window_ordinal = bucket.first_seen_index;
            // WHERE label + §16.6 reservoir→cell cross — both derive from the one dominant
            // component (computed once), always. The label (SRC-D-WHERE-2) is the leaf WHERE; the
            // cube cross (LOCATION-only {level, where}, read-only) feeds the cube. Empty
            // component → disengaged label; cube_location maps it to the aggregated-WHERE star.
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

    // SPEC §3.7 / §8 clause 4: declare the cap AT the site that enforces it — both phases below
    // stop at `config_.reservoir_size`, so the declaration and the bound cannot drift apart.
    stats.reservoir_size = config_.reservoir_size;
    stats.reservoir.reserve(std::min(config_.reservoir_size, candidates.size()));

    // ── Phase 1: the error-class reserve (SRC-D-RNK-2 §5.2) ──
    // A bounded floor of slots admitted to error-class templates by salience (then template_id),
    // EXEMPT from the per-kind cap, BEFORE the general pool — so non-failure salience (novelty /
    // structural-surprise) can never crowd a real failure out of a high-cardinality window. The
    // reserve is for failure DEPTH: a genuine failure storm overflows the reserve and the
    // top-by-salience failures are kept (the salience/template_id order already does this). Clamped
    // to M. 0 = disabled.
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

    // ── Phase 2: the general pool ──
    // Fill the remaining slots in salience order under the per-kind diversity cap. Candidates
    // already taken by the reserve are skipped (they are in `reserved`). The per-kind counters
    // start fresh — the reserve is a separate, exempt budget; this cap governs only the general
    // pool, so an error-class template beyond the reserve still competes here under its kind cap.
    std::unordered_map<std::uint16_t, std::size_t> per_kind;
    for (const auto& candidate : candidates)
    {
        if (stats.reservoir.size() >= config_.reservoir_size)
            break;
        if (reserved.contains(ordered[candidate.index].first))
            continue; // promoted by the reserve in phase 1
        const Bucket& bucket{*ordered[candidate.index].second};
        const auto level{
            dominant_event_level_of(bucket.level_counts, bucket.declared_level_counts)};
        const auto role{dominant_role_of(bucket.role_counts)};
        if (config_.reservoir_per_kind_cap > 0)
        {
            auto& kind_count{per_kind[kind_key(role, level)]};
            if (kind_count >= config_.reservoir_per_kind_cap)
                continue; // this kind is already covered — keep M for other kinds
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
    // SPEC §4 `dropped_ngram_observations` — declared here, at the window whose accounting bound
    // it describes, and OMITTED when that bound refused nothing. The omission is the whole point:
    // §4 reads an absent key in a 0.7.0+ document as "no observations were dropped", so a window
    // that stayed under its cap keeps emitting exactly the bytes it emitted before this field
    // existed. Writing a 0 would spend a key to say what the silence already says, and the
    // published schema's `minimum: 1` rejects it.
    //
    // Read BEFORE the snapshot in close_window: this runs inside the same close, so the live
    // counter is still the window's own. The block-absent early return above is the field's one
    // boundary — with no `behavior` there is no §4 block to carry the count, and a document that
    // omits the block claims nothing about n-grams either way. The loss is still stated on that
    // path, by the pipeline's per-window WARN (insight-eidos, ADR-9.D3).
    if (ngram_observations_dropped_ > 0)
        behavior.dropped_ngram_observations = ngram_observations_dropped_;
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
                // u64 count widened VALUE-PRESERVING via u128, matching native.
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
    // SPEC §4.2 / §8 clause 4: an OMITTED `branching_size` asserts "no cap"; this producer caps,
    // so the declaration is owed on every document carrying the block, and it is made here, at
    // the truncation that enforces it.
    behavior.branching_size = config_.top_branching_size;
}

// dominant_path (SPEC §4.1): greedy highest-count walk from the busiest template.
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

// Highest-count template (ties → lower internal id) — the dominant_path start node.
MetaLogEngine::InternalTemplateID MetaLogEngine::dominant_path_start() const
{
    InternalTemplateID start_id{0};
    std::uint64_t best_count{0};
    // Iterate the (string-keyed) buckets and resolve each to its internal id; the
    // (count desc, internal-id asc) tie-break makes the result iteration-order-independent
    // (the engine keys buckets_ by content_id; the id maps via content_template_index_).
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
    // ── stability block ──
    // Only emitted from the second window onwards (we need a previous
    // window's frequencies to diverge from). The producer-defined
    // stability_score is 1 - js_divergence, in [0, 1] with log2 JS.
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

// Per-window acquisition self-assessment (SRC-D-WHERE-4/SRC-D-WHERE-5): the `component`-axis
// coverage seed, aggregated in batch over the frozen window from the per-template component
// marginals. records_with_component = total located events (Σ over buckets of Σ of
// component_counts — every increment was a non-empty component, ingest_event);
// distinct_components = the size of the union of component values. Both are
// order-independent (a sum / a set-cardinality), so the block is bit-identical across
// stdlibs despite the unordered_map iteration order — the same determinism discipline
// as dominant_component_of. Integer-only, no float, no wall-clock (§16.9). The
// consumer (the Sift diff) applies the coverage/cardinality PREDICATE; the producer
// only states the facts.
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

    // WHERE-tree distinct-cardinality-per-depth (§6.1.1): the WHERE axis is a single depth-1
    // chain today, so the per-depth vector is [distinct_components] (coarsest == finest). It
    // grows once the WHERE tree deepens; the collapse reads it to pick a truncation depth.
    acquisition.where_cardinality_per_depth.push_back(acquisition.distinct_components);

    // Per-dimension cardinality (level, role) + P_closed, read off the closed cube (build_cube ran
    // first). The per-window collapse guardrail's trigger inputs + the mandatory cardinality
    // signal. Pure integer, order-independent (set-cardinalities), so bit-identical cross-stdlib
    // (§16.9). (WHERE cardinality == distinct_components above == card.per_axis[Component], so not
    // re-stored.)
    const CubeCardinalityStat card{cube_cardinality(doc.cube)};
    acquisition.closed_cells = card.cells;
    acquisition.level_cardinality = card.per_axis[static_cast<std::size_t>(CardinalityAxis::Level)];
    acquisition.role_cardinality = card.per_axis[static_cast<std::size_t>(CardinalityAxis::Role)];

    // Span-native facts (SRC-D-OTEL-13 licence + SRC-D-OTEL-11): raw integer counts,
    // threshold-free.
    acquisition.span_records = span_records_;
    acquisition.orphan_parent_edges = orphan_parent_edges_;
    acquisition.orphan_link_edges =
        orphan_link_edges_; // O4b (SRC-D-OTEL-9): cross-route link loss, declared

    doc.acquisition = acquisition;
}

// O4b distilled service topology (SRC-D-OTEL-21). Emitted iff the window had trace substrate
// (span_records_ > 0) — a non-span window omits the block (absence = unknown; the edge diff needs
// it on both sides). service_edges_ is already in canonical (caller, callee) order (std::map); emit
// the top `max_service_edges` by weight (canonical-key tie-break), sorted back into canonical order
// for a deterministic wire, with dropped_edges = the honest truncation count. A present-but-empty
// block (traces existed, no cross-service edges) is meaningful — it says "no topology", not
// "unknown".
void MetaLogEngine::build_service_edges(MetaLogDocument& doc) const
{
    if (span_records_ == 0)
        return; // no trace substrate → omit the block (SRC-D-OTEL-20)

    ServiceEdgeBlock block;
    if (service_edges_.size() <= config_.max_service_edges)
    {
        block.edges.reserve(service_edges_.size());
        for (const auto& [pair, weight] : service_edges_) // std::map → already canonical order
            block.edges.push_back(
                ServiceEdge{.caller = pair.first, .callee = pair.second, .weight = weight});
    }
    else
    {
        // Over the cap: keep the top `max_service_edges` by weight, canonical-key tie-break. Select
        // on a view (weight desc, then canonical key asc), take the cut, then re-sort into
        // canonical order.
        std::vector<std::pair<std::pair<std::string, std::string>, std::uint64_t>> view(
            service_edges_.begin(), service_edges_.end());
        std::ranges::sort(view,
                          [](const auto& lhs, const auto& rhs)
                          {
                              if (lhs.second != rhs.second)
                                  return lhs.second > rhs.second; // heavier first
                              return lhs.first < rhs.first;       // canonical key tie-break
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
    // Stash this window's frequency map for the NEXT close_window's
    // stability computation, then drop the rest of the per-window state.
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
    // SRC-D-OTEL-11: per-window span state resets with the trace state
    span_templates_.clear();
    span_fifo_.clear();
    pending_span_edges_.clear();
    span_records_ = 0;
    orphan_parent_edges_ = 0;
    orphan_link_edges_ = 0;
    pending_link_edges_.clear(); // O4b Span Links (SRC-D-OTEL-9): per-window link state resets too
    service_edges_
        .clear(); // O4b (SRC-D-OTEL-21): per-window service topology resets with the span state
    ngram_counts_.clear();
    ngram_total_ = 0;
    ngram_observations_dropped_ = 0; // per-window like the table it guards (ADR-9.D3)
    cube_base_.clear();
}

} // namespace insight::metalog
