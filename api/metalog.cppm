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

    // Account one observed canonical event. The first occurrence of a
    // given template_id memorises its template_str (later occurrences
    // only bump the counter — template strings are arena-stable in
    // CanonicalEvent and assumed identical for identical IDs).
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
        // Preserved as the minimum across Drain cluster migration (earliest wins).
        std::uint64_t first_seen_index{0};
        std::unordered_map<LogLevel, std::uint64_t> level_counts;
        // Announced structural roles seen for this template (→ salience).
        // Dominant role feeds the salience severity signal (Terminator = severe).
        std::unordered_map<StructuralRole, std::uint64_t> role_counts;
        // Functional-source (canon `component`) counts seen for this template.
        // Populated only when config_.emit_cube. The dominant component is the
        // template's WHERE-path for the §16.6 reservoir→cell cross. Not the cube
        // itself (the cube is the per-EVENT joint, accumulated in cube_base_).
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
        std::string template_str;
        std::string content_id;
        InternalTemplateID internal_id{};
    };

    [[nodiscard]] TemplateLookup content_template_id_for(const tokenization::CanonicalEvent& event);
    // Re-attribute a bucket when a Drain cluster's template evolves mid-window
    // (e.g. its first literal occurrence later gains a wildcard): merge the prior
    // occurrences from the old content_id into the new one so the cluster stays a
    // single template, never a stray literal singleton.
    void migrate_bucket(const std::string& from_content_id, const std::string& to_content_id,
                        std::string_view new_template_str);
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
    void stash_prev_window(const MetaLogDocument& doc);
    void build_templates_map(MetaLogDocument& doc) const;
    void reset_window_state();

    MetaLogConfig config_{};
    SourceBlock source_{};
    std::optional<Timestamp> window_start_;
    std::uint64_t lines_observed_{0};
    std::unordered_map<std::string, Bucket> buckets_;
    std::unordered_map<TemplateID, TemplateCacheEntry> template_id_cache_;
    std::unordered_map<std::string, InternalTemplateID> content_template_index_;
    std::vector<std::string> content_templates_by_internal_id_;

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
    std::unordered_map<std::string, std::uint64_t> prev_freq_;
    std::uint64_t prev_total_{0};
    std::optional<std::string> prev_window_end_iso_;

    // HyperLogLog sketches for approximate cardinality per (template, param_index).
    // Pimpl to avoid exposing HLL internals in the public header.
    // Defined in metalog_engine.cpp; reset at open_window(), snapshotted at close_window().
    struct HllState;
    std::unique_ptr<HllState> hll_state_;
};

// Free serialiser. Produces a serialised JSON document conforming to the
// v0.5.0 MetaLog envelope.
//
// Output is canonical and restrictive: empty/default optional fields are
// OMITTED, never emitted as empty/zero/false (one document -> one byte
// sequence). Consumers MUST treat an absent field as equivalent to its
// empty/zero/false value (SPEC §0: producers omit, consumers read lenient).
[[nodiscard]] std::string to_json(const MetaLogDocument& doc);

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
[[nodiscard]] MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs);

// Compute the pair-wise difference between two MetaLog documents.
// `previous` is the earlier document; `current` is the later one.
// `delta = current - previous`. See SPEC §13.
[[nodiscard]] MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current);

} // namespace insight::metalog
