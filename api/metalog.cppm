// refs: ADR-3.D4
export module insight.metalog;
import insight.metalog.internal;
import insight.canon;
export import insight.metalog.api;

export namespace insight::metalog
{

// invariant: not thread-safe, and at most one window is open at a time.
class MetaLogEngine
{
  public:
    MetaLogEngine();
    explicit MetaLogEngine(MetaLogConfig config);
    ~MetaLogEngine();
    MetaLogEngine(const MetaLogEngine&) = delete;
    MetaLogEngine& operator=(const MetaLogEngine&) = delete;
    MetaLogEngine(MetaLogEngine&&) = delete;
    MetaLogEngine& operator=(MetaLogEngine&&) = delete;

    // post: the produced document's source block stays empty until this is called.
    void set_source(SourceBlock source);

    // post: per-window state is reset and an in-flight window is discarded, never closed.
    // post: the previous window's frequencies survive, so stability spans open/close.
    void open_window(Timestamp start);

    // pre: event.template_str is canon's content-deterministic identity for the line.
    // post: first sight interns the template; every later occurrence only bumps its count.
    // refs: ADR-16.D5
    void ingest_event(const tokenization::CanonicalEvent& event);

    // post: the engine returns to no-open-window and keeps this window's frequencies for the next
    // window's stability.
    // post: reported_bounds overrides ONLY the document's reported start/end/duration; `end` still
    // drives the window machinery.
    [[nodiscard]] MetaLogDocument
    close_window(Timestamp end, std::optional<ReportedWindowBounds> reported_bounds = std::nullopt);

    // refs: SRC-D-TIR-5
    [[nodiscard]] const TemplateRegistry& registry() const noexcept
    {
        return registry_;
    }

    // pre: read only between close_window() and the next open_window().
    // post: OBSERVATIONS refused at the cap, not distinct keys -- the distinct count is strictly
    // smaller and is not knowable here.
    // note: this count never feeds the deterministic content stream.
    // refs: ADR-9.D3
    [[nodiscard]] std::uint64_t last_window_ngram_observations_dropped() const noexcept
    {
        return last_window_ngram_observations_dropped_;
    }

  private:
    static constexpr std::size_t kMaxTrackedNgramSize = 3;
    using InternalTemplateID = std::uint64_t;

    struct Bucket
    {
        std::string template_str;
        std::uint64_t count{0};
        // invariant: the events ingested in this window before this template was first seen.
        std::uint64_t first_seen_index{0};
        std::unordered_map<LogLevel, std::uint64_t> level_counts;
        // invariant: the same keys as level_counts, counting only events whose level came from a
        // position whose MEANING is the level rather than from content inference.
        // invariant: read by one keyed lookup, never iterated, so the map order reaches no output.
        // refs: DN-32.D3
        std::unordered_map<LogLevel, std::uint64_t> declared_level_counts;
        // invariant: AND-reduced over the window's events, so true iff every event that formed this
        // template was echoed script source and none was a real runtime occurrence.
        // refs: SRC-D-PROV-1
        bool all_echoed_source{true};
        std::unordered_map<StructuralRole, std::uint64_t> role_counts;
        // invariant: always populated; the dominant component is the template's WHERE label.
        // note: not the cube, which is the per-EVENT joint accumulated in cube_base_.
        // refs: SRC-D-WHERE-2, ADR-9.D2
        std::unordered_map<std::string, std::uint64_t, TransparentStringHash, std::equal_to<>>
            component_counts;
        // invariant: index i is CanonicalEvent::params[i]; populated only when
        // config_.max_param_histograms > 0.
        // refs: ADR-9.D2
        std::vector<
            std::unordered_map<std::string, std::uint64_t, TransparentStringHash, std::equal_to<>>>
            param_value_counts;
        std::vector<std::uint64_t> param_totals;
        // invariant: keyed by canon field name, not by param position, so it never collides with
        // param_value_counts -- a field is ordinal XOR categorical.
        // invariant: populated only when config_.max_param_histograms > 0.
        // refs: SRC-D-W1-2, SRC-D-W1-5, ADR-9.D2
        struct OrdinalAccumulator
        {
            OrdinalSchedule schedule{};
            // invariant: sized to the schedule's bin count on the first observation.
            std::vector<std::uint64_t> counts;
            std::uint64_t total{0};
        };
        std::unordered_map<std::string, OrdinalAccumulator, TransparentStringHash, std::equal_to<>>
            ordinal_accumulators;
    };

    struct NGramKey
    {
        std::array<InternalTemplateID, kMaxTrackedNgramSize> ids{};
        std::uint8_t size{};

        [[nodiscard]] bool operator==(const NGramKey& other) const noexcept = default;
    };

    struct NGramKeyHash
    {
        [[nodiscard]] std::size_t operator()(const NGramKey& key) const noexcept;
    };

    // invariant: [0] is the last template id, [1] the one before it.
    struct NgramRing
    {
        std::array<InternalTemplateID, 2> recent{};
        std::size_t filled{0};
    };

    struct TemplateLookup
    {
        const std::string* content_id{nullptr};
        InternalTemplateID internal_id{};
    };

    struct TemplateCacheEntry
    {
        std::string content_id;
        InternalTemplateID internal_id{};
    };

    [[nodiscard]] TemplateLookup content_template_id_for(const tokenization::CanonicalEvent& event);
    void account_ngram(const NGramKey& key);
    // post: the same accounting for the global ring and for each per-trace ring, so the n-gram
    // graph is identical for non-OTEL input and trace-scoped for OTEL input.
    void account_ngram_into(NgramRing& ring, InternalTemplateID internal_id);
    // post: created on first sight under config_.max_active_traces, evicting the oldest-inserted
    // trace first.
    [[nodiscard]] NgramRing& trace_ring_for(TraceId trace_id);

    // post: the span's span_id -> template id is remembered under config_.max_active_spans and its
    // declared parent edge is queued for close-time resolution.
    // invariant: a span never enters an adjacency ring -- its causality is declared.
    // refs: SRC-D-OTEL-11, ADR-29.D2
    void record_span(const tokenization::CanonicalEvent& event, InternalTemplateID internal_id);
    // post: each queued parent edge resolves into ngram_counts_ as template -> template; an
    // unresolved parent increments orphan_parent_edges_.
    // invariant: counts are commutative and integer-only with no wall-clock read, so the resolution
    // order cannot move a byte.
    // refs: SRC-D-OTEL-11
    void resolve_span_edges();

    // invariant: computed once per close_window and owned by a local there, so it lives exactly as
    // long as the close it serves.
    // invariant: `k` is the resolved top_k cut, min(top_k_size, ordered.size()).
    struct WindowAnalysis
    {
        std::vector<std::pair<std::string, const Bucket*>> ordered;
        std::unordered_map<InternalTemplateID,
                           std::unordered_map<InternalTemplateID, std::uint64_t>>
            transitions;
        // invariant: indexed by internal id; 0 means on-path or root.
        std::vector<std::uint32_t> incoming_surprise;
        // invariant: entries [0, top_k_cut) of `ordered` are the top-K, the rest the tail.
        std::size_t top_k_cut{0};
    };

    // invariant: a below-top_k template with a non-zero salience; `index` points into
    // WindowAnalysis::ordered.
    // post: admitted in salience order, tie-broken by template_id, under the size and per-kind
    // caps.
    struct ReservoirCandidate
    {
        std::size_t index;
        std::uint32_t salience;
        std::uint32_t structural_surprise;
        std::uint32_t novelty;
        // invariant: the argmax salience_score's axis, stamped so a consumer never re-derives it.
        // refs: DN-64.D3
        std::optional<RetentionAxis> retention_axis;
    };

    // invariant: const steps read window state and write the document; the two non-const steps
    // carry or clear cross-window state.
    // refs: F-SRC-insight-metalog:test_golden_vectors.cpp
    [[nodiscard]] WindowAnalysis analyze_window() const;
    void build_transition_graph(WindowAnalysis& analysis) const;
    [[nodiscard]] std::uint32_t surprise_of(const WindowAnalysis& analysis,
                                            const std::string& content_id) const noexcept;
    // note: the engine keys per-window state by content_id string; this bridges to the POD.
    // refs: SRC-D-TIR-2
    [[nodiscard]] TemplateId template_id_for(const std::string& content_id) const;
    void stamp_envelope(MetaLogDocument& doc, Timestamp start, Timestamp end,
                        std::optional<ReportedWindowBounds> reported_bounds) const;
    void build_top_k(MetaLogDocument& doc, const WindowAnalysis& analysis) const;
    void build_reservoir(MetaLogDocument& doc, const WindowAnalysis& analysis,
                         std::unordered_set<std::string>& reserved) const;
    [[nodiscard]] std::vector<ReservoirCandidate>
    collect_reservoir_candidates(const WindowAnalysis& analysis) const;
    void admit_reservoir(StatsBlock& stats, const WindowAnalysis& analysis,
                         std::vector<ReservoirCandidate>& candidates,
                         std::unordered_set<std::string>& reserved) const;
    void build_tail_and_entropy(MetaLogDocument& doc, const WindowAnalysis& analysis,
                                const std::unordered_set<std::string>& reserved) const;
    void build_behavior(MetaLogDocument& doc, const WindowAnalysis& analysis) const;
    void build_top_ngrams(BehaviorBlock& behavior) const;
    void build_branching(BehaviorBlock& behavior, const WindowAnalysis& analysis) const;
    void build_dominant_path(BehaviorBlock& behavior, const WindowAnalysis& analysis) const;
    [[nodiscard]] InternalTemplateID dominant_path_start() const;
    void build_stability(MetaLogDocument& doc, const WindowAnalysis& analysis) const;
    // post: stamps the one-window element on every retained row -- top_k union reservoir -- and
    // opens the document's roll-up.
    // pre: the tail is already built, since the roll-up's honesty rests on knowing it.
    // refs: DN-50.D4
    void build_presence_churn(MetaLogDocument& doc) const;
    // post: always built, collapse-bounded, from the per-event joint in cube_base_.
    void build_cube(MetaLogDocument& doc) const;
    // post: always built, from the buckets' component marginals.
    // refs: SRC-D-WHERE-4, SRC-D-WHERE-5
    void build_acquisition(MetaLogDocument& doc) const;
    // post: emitted iff the window had trace substrate, sorted canonical order, top
    // max_service_edges by weight with a canonical-key tie-break, plus dropped_edges.
    // refs: SRC-D-OTEL-21
    void build_service_edges(MetaLogDocument& doc) const;
    void stash_prev_window(const MetaLogDocument& doc);
    void reset_window_state();

    MetaLogConfig config_{};
    SourceBlock source_{};
    std::optional<Timestamp> window_start_;
    std::uint64_t lines_observed_{0};
    std::unordered_map<std::string, Bucket> buckets_;
    // invariant: keyed on the content-deterministic template_str, so it saves only the repeat
    // SHA-256 -- one template_str maps to exactly one content_id.
    std::unordered_map<std::string, TemplateCacheEntry, TransparentStringHash, std::equal_to<>>
        template_str_cache_;
    std::unordered_map<std::string, InternalTemplateID> content_template_index_;
    // invariant: indexed by internal id; the h-prefixed hex string is rendered only at the
    // serialize seam.
    // refs: SRC-D-TIR-2
    std::vector<TemplateId> content_templates_by_internal_id_;
    // refs: SRC-D-TIR-5
    TemplateRegistry registry_;

    // invariant: the non-OTEL n-gram path; only [0] is read at ngram_size 2, both at 3.
    NgramRing global_ring_{};
    // invariant: one ring per active OTEL trace, bounded by config_.max_active_traces with FIFO
    // eviction ordered by trace_ring_fifo_; empty for a non-OTEL stream.
    // invariant: point-lookup only, never iterated, so the map order reaches no output.
    // refs: SRC-D-OTEL-1
    std::unordered_map<TraceId, NgramRing> trace_rings_;
    std::deque<TraceId> trace_ring_fifo_;

    // invariant: point-lookup only, never iterated; span_fifo_ carries the eviction order and
    // pending_span_edges_ the ingest order, so nothing rests on the map's own order.
    // invariant: a declared parent resolves into the SAME bounded ngram_counts_ graph, so there is
    // one fingerprint and no second graph.
    // note: component is owned; canon's string_view is arena-stable only in the record.
    // refs: SRC-D-OTEL-11, SRC-D-OTEL-21, ADR-29.D2
    struct SpanNode
    {
        InternalTemplateID template_id{};
        std::string component;
    };
    std::unordered_map<SpanId, SpanNode> span_templates_;
    std::deque<SpanId> span_fifo_;
    struct PendingSpanEdge
    {
        InternalTemplateID child_template{};
        SpanId parent_span_id{};
        // invariant: the CHILD span's service.name, which is the service edge's callee.
        std::string child_component;
    };
    std::vector<PendingSpanEdge> pending_span_edges_;
    // post: resolved at close by span_id, across traces, into the same distilled service topology
    // as intra-trace parentage.
    // note: only the source component is known now; the linked span may come later.
    // refs: SRC-D-OTEL-9
    struct PendingLinkEdge
    {
        std::string source_component;
        SpanId linked_span_id{};
    };
    std::vector<PendingLinkEdge> pending_link_edges_;
    // refs: SRC-D-OTEL-13
    std::uint64_t span_records_{0};
    // invariant: declared parents that did not resolve.
    // refs: SRC-D-OTEL-11
    std::uint64_t orphan_parent_edges_{0};
    // invariant: declared LINK targets that did not resolve -- counted, never guessed.
    // refs: SRC-D-OTEL-9
    std::uint64_t orphan_link_edges_{0};
    // invariant: accumulated from each resolved parent-child pair whose components differ,
    // self-edges excluded, and cleared per window.
    // invariant: a std::map, so iteration is the canonical (caller, callee) byte order the wire
    // block emits.
    // refs: SRC-D-OTEL-21
    std::map<std::pair<std::string, std::string>, std::uint64_t> service_edges_;

    // invariant: keyed by compact per-window internal ids; the content-derived spec ids are
    // substituted only when the document is built.
    std::unordered_map<NGramKey, std::uint64_t, NGramKeyHash> ngram_counts_;
    std::uint64_t ngram_total_{0};
    // invariant: observations refused at the config_.max_ngram_keys cap this window.
    // refs: ADR-9.D3
    std::uint64_t ngram_observations_dropped_{0};
    // invariant: snapshotted at close_window BEFORE reset_window_state clears the window, so a
    // consumer can still read it after close_window returns.
    std::uint64_t last_window_ngram_observations_dropped_{0};

    // invariant: the per-EVENT joint, one insert per ingest, keyed on api and canon types only; the
    // closed cube is built from it in build_cube.
    std::map<std::tuple<LogLevel, std::string, StructuralRole>, std::uint64_t,
             TransparentCubeKeyLess>
        cube_base_;

    // invariant: written at the end of each close_window and read at the start of the next.
    std::unordered_map<TemplateId, std::uint64_t> prev_freq_;
    std::uint64_t prev_total_{0};
    std::optional<std::string> prev_window_end_iso_;

    // invariant: pimpl, so no HLL internal reaches this interface; reset at open_window and
    // snapshotted at close_window.
    struct HllState;
    std::unique_ptr<HllState> hll_state_;
};

// post: the returned bytes are legal RFC 8259 JSON for every input, a control byte in a value being
// emitted escaped rather than raw.
// post: canonical and restrictive -- an empty or default optional field is omitted, so one document
// yields one byte sequence.
// pre: `registry` contains every id the document references; engine.registry() does.
// note: this seam emits the per-entry inline template mode; the others were never wired.
// refs: ADR-9.D4, DN-65.D1, DN-65.D5, SRC-D-TIR-5
[[nodiscard]] std::string to_json(const MetaLogDocument& doc, const TemplateRegistry& registry);

// post: the same omit-empty discipline and the same RFC 8259 guarantee as the document overload,
// both writing through the one entry point that forces the escape.
[[nodiscard]] std::string to_json(const MetaLogDiff& diff);

// post: required fields are preserved -- lines_observed, the unique_templates union and the
// time-axis envelope; a non-empty tail on either input makes the merge lossy.
// post: the composed caps are the MINIMUM over the caps the inputs declared, an input declaring
// none being skipped rather than read as zero.
// invariant: stability is dropped and the result is id-only, so the display string resolves by id
// from the engine registry.
// refs: DN-56.D2, DN-56.D3, SRC-D-TIR-5
[[nodiscard]] MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs);

// post: delta is current minus previous, with `previous` the earlier document.
[[nodiscard]] MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current);

// post: derived from the diff's own findings rather than stored, one clause per optional signal
// property, each mirroring that property's schema vacuity declaration.
// invariant: the vacuity declarations assert exact equality, so the scalar tests are exact float
// compares by contract.
// note: a signal property added to the schema must gain a clause here in the same pass.
// refs: F-SRC-insight-metalog:spec_conformance_gate.sh
[[nodiscard]] ComparisonOutcome comparison_outcome_of(const MetaLogDiff& diff) noexcept;

// post: the signal properties in which this comparison found a change the serialized document does
// not carry, sorted ascending and duplicate-free.
// invariant: every member is in the witness set, is not withheld_signals itself, and does not
// already witness in the document.
// note: it reports a FINDING, never an inventory of what this producer omits.
[[nodiscard]] std::vector<std::string> withheld_signals_of(const MetaLogDiff& diff);

// post: a pure function of the closed cube's counts that never feeds deterministic content.
// note: the consuming pipeline emits the gated warning; this package excludes the logger.
[[nodiscard]] CubeCardinalityStat cube_cardinality(const CubeBlock& cube);

// post: a human note naming the axis and depth when the collapse guardrail fired, std::nullopt
// otherwise; a pure function of the cube's axes.
// note: observability only -- a coarsened cube binds cross-cube comparability there.
[[nodiscard]] std::optional<std::string> collapse_note(const CubeBlock& cube);

} // namespace insight::metalog
