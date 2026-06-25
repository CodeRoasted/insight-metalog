// insight.metalog — public facade (1.5.1 unwrap §11.9). Consumers `import insight.metalog;`
// unchanged. Re-exports the api DTOs; detail is NOT re-exported. MetaLogEngine +
// compose/diff/to_json live HERE (their impls need detail, which imports api → §11.9.6 homes them
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
    // stateless_template_id.md); the first occurrence of a given template_str
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

    // Compute the spec-conformant template_id for a canonical template
    // string: "h:" + lower_hex(SHA-256(utf8)[0:16]).
    [[nodiscard]] static std::string compute_template_id(std::string_view canonical_template);

    // The TemplateId -> template_str registry (D-TIR-5): the single home of the display-only
    // template_str, accumulated across windows. Injected at the serialize/explain seams to resolve
    // the strings the per-window documents no longer carry.
    [[nodiscard]] const TemplateRegistry& registry() const noexcept { return registry_; }

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
        // Announced structural roles seen for this template (→ salience).
        // Dominant role feeds the salience severity signal (Terminator = severe).
        std::unordered_map<StructuralRole, std::uint64_t> role_counts;
        // Functional-source (canon `component`) counts seen for this template.
        // Populated when config_.emit_cube || config_.emit_where. The dominant
        // component is the template's WHERE label (the cube-independent Sift carrier,
        // D-WHERE-2) and its §16.6 reservoir→cell WHERE-path. Not the cube itself
        // (the cube is the per-EVENT joint, accumulated in cube_base_ under emit_cube).
        std::unordered_map<std::string, std::uint64_t> component_counts;
        // Per-param value histograms; index i == CanonicalEvent::params[i].
        // Populated only when config_.max_param_histograms > 0.
        std::vector<std::unordered_map<std::string, std::uint64_t>> param_value_counts;
        std::vector<std::uint64_t> param_totals;
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

    // Transparent hash so the per-event template_str_cache_ lookup takes a
    // std::string_view (the arena-stable CanonicalEvent::template_str) WITHOUT
    // constructing a std::string on the hot path — heterogeneous find/contains.
    // Insertion (a cache miss, once per distinct template) still owns the key.
    struct TransparentStringHash
    {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept
        {
            return std::hash<std::string_view>{}(key);
        }
    };

    [[nodiscard]] TemplateLookup content_template_id_for(const tokenization::CanonicalEvent& event);
    void account_ngram(const NGramKey& key);

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
        std::size_t k{0};
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
    // entries carry — D-TIR-2). Cold path: called per top_k/reservoir entry.
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
    // role) joint accumulated in cube_base_. Only when config_.emit_cube.
    void build_cube(MetaLogDocument& doc) const;
    // Per-window acquisition self-assessment (D-WHERE-4/5): the window's integer
    // structural facts (the `component`-axis coverage seed), aggregated from the
    // buckets' component marginals. Only when config_.emit_where || config_.emit_cube.
    void build_acquisition(MetaLogDocument& doc) const;
    void stash_prev_window(const MetaLogDocument& doc);
    void build_templates_map(MetaLogDocument& doc) const;
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
    // internal_id → the template's canon TemplateId POD (D-TIR-2). build_top_ngrams /
    // build_branching / build_dominant_path index this to stamp the domain id without
    // re-hashing; the "h:"+hex string is rendered only at the serialize seam.
    std::vector<TemplateId> content_templates_by_internal_id_;
    // D-TIR-5: the single TemplateId -> template_str home (display-only), accumulated across windows
    // and injected at the serialize/explain seams. template_str is dropped from the per-window
    // document entries that flow through the pyramid.
    TemplateRegistry registry_;

    // Recent internal template ID ring (size 2): [0] = last, [1] = second-last.
    // Only [0] is used at ngram_size=2; both are used at ngram_size=3.
    std::array<InternalTemplateID, 2> recent_{};
    std::size_t recent_filled_{0};

    // n-gram table: compact per-window internal template IDs -> count. We translate to
    // content-derived spec IDs only when building the document.
    std::unordered_map<NGramKey, std::uint64_t, NGramKeyHash> ngram_counts_;
    std::uint64_t ngram_total_{0};

    // Intra-window cube base (SPEC §16): the per-EVENT joint (level, component, role) →
    // count. Populated only when config_.emit_cube (one map insert per ingest, off the
    // default hot path). An ordered map keyed on api/canon types only (no detail type in
    // the facade interface); the closure is built from it at close_window in build_cube.
    std::map<std::tuple<LogLevel, std::string, StructuralRole>, std::uint64_t> cube_base_;

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

// Free serialiser. Produces a serialised JSON document conforming to the
// v0.6.0 MetaLog envelope.
//
// Output is canonical and restrictive: empty/default optional fields are
// OMITTED, never emitted as empty/zero/false (one document -> one byte
// sequence). Consumers MUST treat an absent field as equivalent to its
// empty/zero/false value (SPEC §0: producers omit, consumers read lenient).
// D-TIR-5 field-drop (cascade Stage 2): the display-only `template_str` is sourced from the
// engine-owned TemplateRegistry at this seam (prefer-registry / field-fallback). The registry param is
// defaulted to an empty table while the cascade lands — engine-built docs pass `engine.registry()` to
// exercise the registry path; hand-built docs fall back to the per-entry field, byte-identical. Stage 4
// drops the default + the per-entry field, making the registry a required serialise input.
[[nodiscard]] std::string to_json(const MetaLogDocument& doc,
                                  const TemplateRegistry& registry = TemplateRegistry{});

// Free serialiser for the diff document (SPEC §13). Same restrictive,
// omit-empty discipline as the document serialiser above.
[[nodiscard]] std::string to_json(const MetaLogDiff& diff);

// ── Composition and diff (SPEC §12, §13) ───────────────────────

// Merge two MetaLog documents into one. Lossy when either input had a
// non-empty tail; required fields are preserved (lines_observed,
// unique_templates union, time-axis envelope). See SPEC §12.
//
// `top_k_size` and `top_ngrams_size` come from `lhs`; the result is
// truncated to those sizes. `template_emission` comes from `lhs` too.
//
// Stability is dropped (meaningless across composed inputs).
// `keep_template_str` (default true) materialises the display-only `template_str` on the merged
// document. Pass FALSE when the result is consumed only by the id-based path (the pyramid's
// diff-only baselines) to skip the O(Σcompose × templates) copy — template_str lives outside the
// pyramid, resolved at display from the engine registry (D-TIR-5, insight_perf_template_id.md §6).
[[nodiscard]] MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs,
                                      bool keep_template_str = true);

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

} // namespace insight::metalog
