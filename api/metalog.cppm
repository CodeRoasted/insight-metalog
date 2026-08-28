// insight.metalog — public facade (ADR-3.D4). Consumers `import insight.metalog;`
// unchanged. Re-exports the api DTOs; detail is NOT re-exported. MetaLogEngine +
// compose/diff/to_json live HERE (their impls need detail, which imports api → ADR-3.D4 homes them
// above detail to break the cycle).
export module insight.metalog;
import insight.metalog.internal;   // std for the engine/free-fn decls
import insight.canon;              // canon types in MetaLogEngine members
export import insight.metalog.api; // DTOs

export namespace insight::metalog
{
// ── Engine ─────────────────────────────────────────────────────

// Single-window producer. Not thread-safe.
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

    // Optional source metadata (service/fleet/tags). If unset, the
    // produced document carries an empty source block.
    void set_source(SourceBlock source);

    // Begin a window. Resets per-window state. Calling open_window
    // discards any in-flight window. Stability state (previous
    // window's frequencies) is preserved across open/close.
    void open_window(Timestamp start);

    // Account one observed canonical event. The event's template_str is the
    // content-deterministic identity (a pure function of the line's masked tokens —
    // ADR-16.D5); the first occurrence of a given template_str
    // memorises it and later occurrences only bump the counter.
    void ingest_event(const tokenization::CanonicalEvent& event);

    // Close the current window and produce a spec-conformant document.
    // After this call the engine returns to the "no open window"
    // state but retains the previous-window frequencies needed to
    // compute stability for the next window.
    // `reported_bounds`, when set, overrides ONLY the document's reported window
    // start/end/duration (the deterministic parseable-ts envelope); the close
    // `end` still drives window machinery. See ReportedWindowBounds.
    [[nodiscard]] MetaLogDocument
    close_window(Timestamp end, std::optional<ReportedWindowBounds> reported_bounds = std::nullopt);

    // SRC-D-TIR-5 — see metalog.api.cppm (TemplateRegistry) for the contract.
    [[nodiscard]] const TemplateRegistry& registry() const noexcept
    {
        return registry_;
    }

    // How many n-gram OBSERVATIONS the window just closed discarded at the `max_ngram_keys` cap
    // (ADR-9.D3: adaptive lossiness never hides data loss). Valid between close_window() and the
    // next open_window(); 0 means the cap never bound, which is the ordinary case at 4096.
    //
    // OBSERVATIONS, not distinct keys, and the distinction is not pedantry. account_ngram() drops
    // BEFORE inserting, so a key rejected once is rejected again on every later occurrence — the
    // count is occurrences lost, and the number of distinct n-grams behind it is strictly smaller
    // and NOT knowable here. Knowing it would mean remembering the dropped keys, which is exactly
    // the unbounded set the cap exists to refuse. Reporting this as "keys" would overstate the
    // structural loss and understate the observational one.
    //
    // THE QUANTITY RIDES THE WIRE (SPEC §4 `behavior.dropped_ngram_observations`, set in
    // build_behavior); this ACCESSOR is the observability path and nothing else, and the two are
    // not redundant. metalog excludes spdlog by design, so the count lives here and the WARN fires
    // where logging does, in the eidos pipeline at window close (Founder ruling 2026-06-20, the
    // same split the §13 cardinality monitor uses) — and this accessor is the ONLY reader that
    // survives the case the wire field cannot cover, a window whose `behavior` block is omitted
    // (top_ngrams_size = 0) and which therefore has no §4 block to carry the count.
    // Neither reader feeds the deterministic content stream.
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
        // Ordinal of the event at which this template was first seen in the window
        // (== lines_observed_ before that event). Feeds the self-novelty axis: a
        // high value means the template EMERGED late (first-seen near lines_observed).
        std::uint64_t first_seen_index{0};
        std::unordered_map<LogLevel, std::uint64_t> level_counts;
        // DN-32.D3 — the DECLARED half of level_counts: the same keys, counting only the events
        // whose level came from a position whose MEANING is the level (canon's declared layer)
        // rather than from its content-inference layer. A template's dominant level is
        // CORROBORATED iff this map holds a nonzero count at that level; absent one, the level is
        // canon's guess and a claim resting on it may not contradict a declared run outcome.
        //
        // A SIBLING MAP RATHER THAN A RICHER KEY, on purpose: keying level_counts by
        // (level, declared) would split one template's Error observations into two buckets and
        // silently change which level wins dominance — the tie-break in dominant_level_of reads
        // COUNTS, and a split count is a different number. This carries the new fact without
        // moving any existing one, so every shipped level value is byte-identical.
        //
        // Deterministic: a single keyed lookup, never an iteration, so the unordered_map's order
        // never reaches an output.
        std::unordered_map<LogLevel, std::uint64_t> declared_level_counts;
        // SRC-D-PROV-1 (§3.1): true iff EVERY event that formed this template was echoed script
        // source (no real runtime occurrence). AND-reduced over the events (order-independent →
        // deterministic). When true, the salience failure-cue tier is skipped — the level-blind
        // tier would otherwise re-promote an echoed `…failed…` template that A1 already demoted
        // to Unknown level. A template seen even once as a real runtime event is NOT all-echoed,
        // so its genuine level/role salience stands.
        bool all_echoed_source{true};
        // Announced structural roles seen for this template (→ salience).
        // Dominant role feeds the salience severity signal (Terminator = severe).
        std::unordered_map<StructuralRole, std::uint64_t> role_counts;
        // Functional-source (canon `component`) counts seen for this template.
        // Always populated. The dominant component is the template's WHERE label (the
        // cube-independent Sift leaf carrier, SRC-D-WHERE-2) and its §16.6 reservoir→cell
        // WHERE-path. Not the cube itself (the cube is the per-EVENT joint, in cube_base_).
        //
        // TRANSPARENT hash/eq (ADR-9.D2): a steady-state hit is looked up by string_view
        // and constructs no key — measured at 1 general-heap allocation per event for a
        // >SSO component before this (bench_cube_key_alloc). std::hash<string> ==
        // std::hash<string_view> over equal bytes is a C++17 GUARANTEE, so bucket layout
        // is unchanged — and dominant_component_of is a pure function of contents anyway
        // (its own comment says so), so no observable byte can move.
        std::unordered_map<std::string, std::uint64_t, TransparentStringHash, std::equal_to<>>
            component_counts;
        // Per-param value histograms; index i == CanonicalEvent::params[i].
        // Populated only when config_.max_param_histograms > 0.
        std::vector<std::unordered_map<std::string, std::uint64_t>> param_value_counts;
        std::vector<std::uint64_t> param_totals;
        // W1 ordinal accumulator (§4A.4 SRC-D-W1-2): per declared ordinal field (canon
        // kOrdinalFieldCatalog) seen on this template, its schedule + binned counts over the
        // schedule's log2 ladder. field_name → accumulator. Populated only when
        // config_.max_param_histograms > 0; field-keyed (not positional), so it never collides
        // with param_value_counts (a field is ordinal XOR categorical, SRC-D-W1-5).
        struct OrdinalAccumulator
        {
            OrdinalSchedule schedule{};
            std::vector<std::uint64_t> counts; // sized to the schedule's B on first observation
            std::uint64_t total{0};
        };
        std::unordered_map<std::string, OrdinalAccumulator> ordinal_accumulators;
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

    // The recent-template-id ring an n-gram is formed over (size 2): [0]=last, [1]=2nd-last.
    // One global ring for non-OTEL ingest (the pre-OTEL behaviour, byte-identical), and one
    // ring PER ACTIVE TRACE for OTEL inputs (O2) so a bigram/trigram is formed WITHIN a
    // transaction, not across the global concurrent interleave. NOT a per-trace sub-fingerprint
    // (OR3) — just the two ids needed to close the next edge into the shared global graph.
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
    // Form + account the n-gram(s) ending at `internal_id` over `ring`, then shift it into the
    // ring. O2: the SAME logic for the global ring (non-OTEL) and each per-trace ring (OTEL), so
    // the n-gram graph is identical for non-OTEL inputs and trace-scoped for OTEL inputs.
    void account_ngram_into(NgramRing& ring, InternalTemplateID internal_id);
    // The per-trace ring for `trace_id` (O2), creating it on first sight under the
    // max_active_traces cap with deterministic FIFO eviction of the oldest-inserted trace.
    [[nodiscard]] NgramRing& trace_ring_for(TraceId trace_id);

    // SRC-D-OTEL-11: remember a span's span_id → template id (bounded FIFO under
    // max_active_spans) and queue its declared parent edge (if any) for close-time resolution. A
    // span NEVER enters an adjacency ring — its causality is declared, not positional.
    void record_span(const tokenization::CanonicalEvent& event, InternalTemplateID internal_id);
    // SRC-D-OTEL-11: at window close, resolve each queued parent edge against span_templates_
    // and account the observed edge template(parent)→template(child) into ngram_counts_; an
    // unresolved parent (evicted / straddled) increments orphan_parent_edges_. Order-independent
    // (counts are commutative), integer-only, no wall-clock → deterministic.
    void resolve_span_edges();

    // Cold-path scratch computed once per close_window and consumed by the
    // build_* steps below: the count-sorted bucket view, the template transition
    // graph, and the per-template structural-surprise band. RAII — owned by a
    // local in close_window, released when that scope ends. (`k` is the resolved
    // top_k cut, min(top_k_size, ordered.size()).)
    struct WindowAnalysis
    {
        std::vector<std::pair<std::string, const Bucket*>> ordered;
        std::unordered_map<InternalTemplateID,
                           std::unordered_map<InternalTemplateID, std::uint64_t>>
            transitions;
        std::vector<std::uint32_t> incoming_surprise; // indexed by internal id; 0 = on-path/root
        // Cut index into `ordered`: entries [0, top_k_cut) are the top-K, the rest are the tail.
        std::size_t top_k_cut{0};
    };

    // A below-top_k template that scored a non-zero salience; `index` points into
    // WindowAnalysis::ordered. Collected, then admitted to the reservoir in
    // salience order (tie-break by template_id) under the size + per-kind caps.
    struct ReservoirCandidate
    {
        std::size_t index;
        std::uint32_t salience;
        std::uint32_t structural_surprise;
        std::uint32_t novelty;
        // The argmax `salience_score` stamped alongside `salience` — carried to `admit_one` so the
        // admitted entry states WHICH axis retained it (DN-64.D3 row 3), rather than leaving every
        // consumer to re-derive it from the two ordinals above.
        std::optional<RetentionAxis> retention_axis;
    };

    // close_window is an orchestrator over these single-responsibility steps; each
    // is a verbatim slice of the former monolith, so the produced document is
    // bit-identical (gated by DeterminismGate). const steps read window state and
    // write the document; the two non-const steps carry/clear cross-window state.
    [[nodiscard]] WindowAnalysis analyze_window() const;
    void build_transition_graph(WindowAnalysis& analysis) const;
    [[nodiscard]] std::uint32_t surprise_of(const WindowAnalysis& analysis,
                                            const std::string& content_id) const noexcept;
    // The TemplateId POD for a window content_id string (the engine still keys its
    // per-window state by the content_id string; this bridges to the POD the domain
    // entries carry — SRC-D-TIR-2). Cold path: called per top_k/reservoir entry.
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
    // Intra-window closed cube (SPEC §16), built from the per-event (level, component,
    // role) joint accumulated in cube_base_. Always built (collapse-bounded, §C).
    void build_cube(MetaLogDocument& doc) const;
    // Per-window acquisition self-assessment (SRC-D-WHERE-4/SRC-D-WHERE-5): the window's integer
    // structural facts (component-axis coverage + the dimension self-assessment),
    // aggregated from the buckets' component marginals. Always built.
    void build_acquisition(MetaLogDocument& doc) const;
    // O4b distilled service topology (SRC-D-OTEL-21): emit the service_edges block iff the window
    // had trace substrate (span_records_ > 0), from service_edges_ — sorted canonical order, top
    // `max_service_edges` by weight (canonical-key tie-break) + dropped_edges. Absent for a
    // non-span window.
    void build_service_edges(MetaLogDocument& doc) const;
    void stash_prev_window(const MetaLogDocument& doc);
    void reset_window_state();

    MetaLogConfig config_{};
    SourceBlock source_{};
    std::optional<Timestamp> window_start_;
    std::uint64_t lines_observed_{0};
    std::unordered_map<std::string, Bucket> buckets_;
    // Per-window fast path: template_str → {content_id, internal_id}. Keyed on the
    // content-deterministic template_str (no Drain cluster id any more), so it only
    // saves the repeat SHA-256 — a template_str maps to exactly one content_id. The
    // transparent hash lets a hit look up by string_view with zero allocation.
    std::unordered_map<std::string, TemplateCacheEntry, TransparentStringHash, std::equal_to<>>
        template_str_cache_;
    std::unordered_map<std::string, InternalTemplateID> content_template_index_;
    // internal_id → the template's canon TemplateId POD (SRC-D-TIR-2). build_top_ngrams /
    // build_branching / build_dominant_path index this to stamp the domain id without
    // re-hashing; the "h:"+hex string is rendered only at the serialize seam.
    std::vector<TemplateId> content_templates_by_internal_id_;
    // SRC-D-TIR-5 — see metalog.api.cppm (TemplateRegistry) for the contract.
    TemplateRegistry registry_;

    // The global recent-template-id ring — the non-OTEL n-gram path (pre-OTEL behaviour,
    // byte-identical). Only [0] is used at ngram_size=2; both at ngram_size=3.
    NgramRing global_ring_{};
    // O2 trace-scoping (SRC-D-OTEL-1): one ring per active OTEL trace, so an n-gram is formed
    // within a transaction, not across the global concurrent interleave. Bounded by
    // config_.max_active_traces with deterministic FIFO eviction; trace_ring_fifo_ holds the
    // insertion order (oldest at front). Point-lookup only — never iterated, so the
    // unordered_map order is not a determinism surface. Empty for non-OTEL streams.
    std::unordered_map<TraceId, NgramRing> trace_rings_;
    std::deque<TraceId> trace_ring_fifo_;

    // Observed DAG (SRC-D-OTEL-11): a span record's causality is DECLARED, so it never enters an
    // adjacency ring. Instead its span_id → template id is remembered here, and at close_window
    // each span with a declared parent resolves template(parent)→template(child) into the SAME
    // bounded ngram_counts_ graph (one fingerprint, no fork). span_templates_ is point-lookup only
    // (not iterated → order not a determinism surface); span_fifo_ gives deterministic FIFO
    // eviction under config_.max_active_spans; pending_span_edges_ holds (child template, parent
    // span_id) in ingest order — resolved at close. All empty / untouched for non-span streams
    // (zero added cost). A remembered span: its template id (for the observed template→template
    // edge) + its component (service.name — for the O4b distilled service edge, SRC-D-OTEL-21).
    // component is owned (the canon string_view is arena-stable only within the record).
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
        std::string child_component; // the CHILD span's service.name → the service edge's callee
    };
    std::vector<PendingSpanEdge> pending_span_edges_;
    // O4b Span Links (SRC-D-OTEL-9): a span's declared cross-trace edge to another span, resolved
    // at close (by span_id, across traces) into the SAME distilled service topology as intra-trace
    // parentage — source_component → component(linked). The source component is known now; the
    // linked span (and its component) is resolved at close, since it may serialize later or in
    // another trace.
    struct PendingLinkEdge
    {
        std::string source_component;
        SpanId linked_span_id{};
    };
    std::vector<PendingLinkEdge> pending_link_edges_;
    std::uint64_t span_records_{0}; // span events observed this window (SRC-D-OTEL-13 licence)
    std::uint64_t orphan_parent_edges_{0}; // declared parents that did not resolve (SRC-D-OTEL-11)
    // declared LINK targets that did not resolve (SRC-D-OTEL-9); the cross-route
    // link loss the pooling grain hid, counted not guessed
    std::uint64_t orphan_link_edges_{0};
    // O4b distilled service topology (SRC-D-OTEL-21): (caller_component, callee_component) →
    // observed weight, accumulated in resolve_span_edges from each resolved parent→child pair whose
    // components differ (self-edges excluded). std::map keeps the canonical (caller, callee) byte
    // order the wire block emits; bounded by topology² (service.name is the low-card WHERE tier).
    // Cleared per window.
    std::map<std::pair<std::string, std::string>, std::uint64_t> service_edges_;

    // n-gram table: compact per-window internal template IDs -> count. We translate to
    // content-derived spec IDs only when building the document.
    std::unordered_map<NGramKey, std::uint64_t, NGramKeyHash> ngram_counts_;
    std::uint64_t ngram_total_{0};
    // Observations refused at the `max_ngram_keys` cap this window (ADR-9.D3). A COUNTER and not a
    // log line by construction: the cap is crossed once and then every subsequent new key is
    // refused, so warning at the drop site would emit thousands of lines per window ON THE HOT PATH
    // — a log flood in place of a silent loss, which CLAUDE.md's hot-path doctrine forbids just as
    // firmly. One line per window, at close, carrying the total. Do not "improve" this into a
    // per-drop warning. It rides the drop branch that already exists, so a window that never
    // reaches the cap pays nothing.
    std::uint64_t ngram_observations_dropped_{0};
    // Snapshotted from the above at close_window, BEFORE reset_window_state() clears the window, so
    // the consumer can still read it after close_window returns. Survives the reset deliberately.
    std::uint64_t last_window_ngram_observations_dropped_{0};

    // Intra-window cube base (SPEC §16): the per-EVENT joint (level, component, role) →
    // count. Always populated (one map insert per ingest). An ordered map keyed on
    // api/canon types only (no detail type in
    // the facade interface); the closure is built from it at close_window in build_cube.
    std::map<std::tuple<LogLevel, std::string, StructuralRole>, std::uint64_t,
             TransparentCubeKeyLess>
        cube_base_;

    // Cross-window state for stability. Populated at the end of each
    // close_window with this window's per-template counts and end
    // timestamp; consumed at the start of the next close_window.
    std::unordered_map<TemplateId, std::uint64_t> prev_freq_;
    std::uint64_t prev_total_{0};
    std::optional<std::string> prev_window_end_iso_;

    // HyperLogLog sketches for approximate cardinality per (template, param_index).
    // Pimpl to avoid exposing HLL internals in the public header.
    // Defined in metalog_engine.cpp; reset at open_window(), snapshotted at close_window().
    struct HllState;
    std::unique_ptr<HllState> hll_state_;
};

// Free serialiser. Produces a serialised JSON document conforming to the MetaLog
// envelope at `kMetaLogSpecVersion`.
//
// POSTCONDITION — RFC 8259 CONFORMANCE, unconditional and quantified over ALL string inputs. The
// returned bytes are legal JSON whatever the log carried: a value holding U+0000..U+001F is emitted
// ESCAPED, never raw (SPEC §2 Encoding, §8 clause 5). This is a property of the emitting surface,
// never a precondition on an upstream producer (DN-65.D1), and it is what makes Sift's embedding of
// this document as `glz::raw_json` sound by COMPOSITION rather than by luck: the embedder performs
// no parse round-trip (DN-65.D5), so the embedded document's validity IS this postcondition.
// Discharged by `src/serialization/json_egress.hpp` and gated by
// `tests/serialization/test_egress_encoding_conformance.cpp` against an independent validator.
//
// Output is canonical and restrictive: empty/default optional fields are
// OMITTED, never emitted as empty/zero/false (one document -> one byte
// sequence). Consumers MUST treat an absent field as equivalent to its
// empty/zero/false value (SPEC §0: producers omit, consumers read lenient).
// SRC-D-TIR-5 — see metalog.api.cppm (TemplateRegistry) for the contract. Local to this seam: it
// emits SPEC §3.4's inline mode — the per-entry `template`. The three modes are a producer MAY; the
// others were never wired (ADR-9). The registry MUST contain every id the document references (the
// engine interns every template at ingest); pass `engine.registry()`. A hand-built document needs a
// registry seeded with its strings.
[[nodiscard]] std::string to_json(const MetaLogDocument& doc, const TemplateRegistry& registry);

// Free serialiser for the diff document (SPEC §13). Same restrictive,
// omit-empty discipline as the document serialiser above, and the same RFC 8259 postcondition —
// both overloads write through the one entry point that forces the escape.
[[nodiscard]] std::string to_json(const MetaLogDiff& diff);

// ── Composition and diff (SPEC §12, §13) ───────────────────────

// Merge two MetaLog documents into one. Lossy when either input had a
// non-empty tail; required fields are preserved (lines_observed,
// unique_templates union, time-axis envelope). See SPEC §12.
//
// THE COMPOSED DOCUMENT DECLARES ITS OWN CAPS (SPEC §12.1 / DN-56.D2): `top_k_size`,
// `top_ngrams_size` and `reservoir_size` are the MINIMUM over the caps the inputs actually
// declared, and each block is re-admitted under the result. This overload takes no config, so it
// is the spec's pure merge utility — a merge is never finer than its coarsest member. An input
// that declares no cap is skipped rather than read as a bound of zero, and where NEITHER declares
// one the field is omitted (the block is then unbounded and §8 clause 4 makes no claim about it).
// `min` is symmetric, which is what makes §12.2's commutativity MUST hold on the two REQUIRED cap
// fields; it costs §12.2's associativity SHOULD on the reservoir, which is DN-56.D3's ruled and
// disclosed trade — see `tests/operations/test_compose_algebra.cpp`.
//
// Stability is dropped (meaningless across composed inputs). A composed document carries no display
// template_str — it is id-only; the display string resolves by id from the engine registry at the
// SRC-D-TIR-5 — see metalog.api.cppm (TemplateRegistry) for the contract. Local: counts + levels
// (the decision signal) always carry.
[[nodiscard]] MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs);

// Compute the pair-wise difference between two MetaLog documents.
// `previous` is the earlier document; `current` is the later one.
// `delta = current - previous`. See SPEC §13.
[[nodiscard]] MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current);

// §13 cardinality monitor (cube_perf_and_collapse.md C2): the cube's distinct-value counts +
// closed-cell count, as a PURE function of the closed cube. Observability only — a deterministic
// function of the counts that NEVER feeds the deterministic content stream; the consumer (the eidos
// pipeline) emits the gated WARN naming the offending axis. metalog excludes spdlog by design, so
// the compute lives here and the log fires where logging does (Founder ruling 2026-06-20).
[[nodiscard]] CubeCardinalityStat cube_cardinality(const CubeBlock& cube);

// Was a dimensional collapse APPLIED to this closed cube, and if so which axis + to what
// depth/band? Returns a human note ("level banded to floor N; where truncated to depth M") when the
// collapse guardrail actually fired (an axis carries a band_floor > 0 or a floor_depth below its
// full chain), std::nullopt otherwise. This is the meaningful WARN trigger (studies/005
// disposition-D) — the cube coarsened, so cross-cube comparability now binds at that collapse.
// Observability only; the eidos pipeline emits the gated WARN (metalog excludes spdlog). Pure
// function of the cube's axes.
[[nodiscard]] std::optional<std::string> collapse_note(const CubeBlock& cube);

} // namespace insight::metalog
