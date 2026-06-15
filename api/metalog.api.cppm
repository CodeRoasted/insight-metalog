// insight.metalog.api — public DATA surface (the MetaLog DTOs/config/enums, 1.5.1 unwrap §11.9).
// Header-only structs (no impl units) → no api↔detail cycle. std via internal; canon types
// (LogLevel/Timestamp/StructuralRole/CanonicalEvent) via import insight.canon.
export module insight.metalog.api;
import insight.metalog.internal;
import insight.canon;

export namespace insight::metalog
{

// ── Spec envelope (mirrors v0.5.0 schema) ──────────────────────

// Per-template per-wildcard-position value frequency table.
//
// Drain maps variable token positions to <*>, making field values invisible
// to downstream detection.  FieldHistogram re-surfaces the empirical
// distribution P(value | template_id, param_index) for each wildcarded
// position, giving the drift and sequence banks a causal axis to observe.
//
// Example (HTTP template "GET <*> -> <*>"):
//   histogram[0].value_counts = {"/api/users": 800, "/health": 200}
//   histogram[1].value_counts = {"200": 950, "500": 50}
//
// Only populated when MetaLogConfig::max_param_histograms > 0.
// Empty by default — zero overhead on the ingest_event hot path.
struct FieldHistogram
{
    std::uint32_t param_index{0}; // 0-based wildcard position in params[]
    std::unordered_map<std::string, std::uint64_t> value_counts;
    std::uint64_t total{0};   // total events; may exceed sum(value_counts)
                              // when max_histogram_values cap was hit
    double entropy_bits{0.0}; // Shannon entropy over value_counts
    // HyperLogLog estimate of distinct values in this window for this slot.
    // Independent of max_histogram_values — never capped. 0 if not computed
    // (producer did not enable cardinality tracking). See SPEC §3.5.1.
    std::uint64_t approximate_cardinality{0};
};

struct TopKEntry
{
    std::string template_id;  // "h:" + 32 lowercase hex (spec §3.2)
    std::string template_str; // populated in-engine; serialiser may skip per config
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<LogLevel> dominant_level;
    // Empty unless MetaLogConfig::max_param_histograms > 0.
    std::vector<FieldHistogram> field_histograms;
};

// ── Cube (SPEC §16, EXPERIMENTAL) ──────────────────────────────
//
// Intra-window joint categorical condensation: a CLOSED cube over a small, fixed
// set of low-card categorical axes (level × structural_role × where-chain). An
// attributor/projector, not a detector — given events already marked interesting
// elsewhere, it answers "what is the smallest conjunction characterising them".
// ADDITIVE and removable in a single revert (§16.8): emitted only when the
// producer opts in (MetaLogConfig::emit_cube); never the source of truth for any
// 1-D marginal in v0.6.0.

// One axis descriptor (§16.2). kind=="categorical" is a flat low-card category
// (cell value = a string); kind=="chain" is a single-parent roll-up hierarchy
// (§16.3) carrying its ordered chain levels (coarsest first) + the frozen
// floor_depth (cell value = an ordered prefix-path array).
struct CubeAxis
{
    std::string name; // "level" | "structural_role" | "where"
    std::string kind; // "categorical" | "chain"
    std::optional<std::vector<std::string>> chain; // chain only: ordered levels, coarsest first
    std::optional<std::uint32_t> floor_depth;      // chain only: retained depth (≤ len(chain))
    [[nodiscard]] bool operator==(const CubeAxis&) const noexcept = default;
};

// A cell coordinate (§16.4), generic over axes. An ABSENT axis means aggregated
// (`*`). The v0.6.0 reference axes are fixed (level, structural_role, where), so
// the coord is the three optional keys; a future axis is one more optional field
// (the wire object is open over axis names). A `categorical` value is a string; a
// `chain` value (`where`) is an ordered prefix-path array (`[i]` = chain level i).
struct CubeCoord
{
    std::optional<std::string> level;              // categorical: severity
    std::optional<std::vector<std::string>> where; // chain: WHERE prefix-path
    std::optional<std::string> structural_role;    // categorical: KIND-FRAMING marker
    [[nodiscard]] bool operator==(const CubeCoord&) const noexcept = default;
};

// One closed cell: its coordinate + the distributive COUNT measure (integer).
struct CubeCell
{
    CubeCoord coord;
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const CubeCell&) const noexcept = default;
};

// The closed cube block (§16.1). `cells` is the condensed (closed) representation
// in canonical coord-sorted order; the closure regenerates every non-closed cell
// losslessly. `cell_count`/`raw_cell_count` expose the collapse rate (condensation
// measure) the closure achieved. floor_saturation is a degenerate health metric at
// floor_depth=1 (a diff-time concept) and is intentionally omitted in v0.6.0.
struct CubeBlock
{
    std::vector<CubeAxis> axes;
    std::vector<CubeCell> cells;
    std::uint64_t cell_count{0};     // number of closed cells emitted
    std::uint64_t raw_cell_count{0}; // raw (pre-closure) populated-cell count
    [[nodiscard]] bool operator==(const CubeBlock&) const noexcept = default;
};

// Salience Reservoir entry (Tier 2). A template
// retained by intrinsic SALIENCE rather than frequency — where a rare-but-severe
// event (a lone fatal) survives the bounded fingerprint instead of collapsing
// into the tail. Self-describing: carries why it was kept (salience + the inputs)
// so a consumer/explainer can attribute it. Disjoint from top_k (a template here
// did NOT make top_k by frequency); excluded from the tail residual.
struct ReservoirEntry
{
    std::string template_id;
    std::string template_str; // per template_emission mode, like TopKEntry
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<LogLevel> dominant_level;
    StructuralRole structural_role{StructuralRole::None};
    // Structural-surprise band (0..100): how off-path this template is, derived
    // from the lowest-probability incoming transition in the behavior graph. >0
    // means the template was reached only via a rare transition off the dominant
    // path — the axis that retains a benign-but-anomalous event (an Info "took
    // alternate cache path") that severity⊗rarity alone would drop. Attribution:
    // a nonzero value here on a non-severe entry explains the retention.
    std::uint32_t structural_surprise{0};
    // Self-novelty band (0..100): how late this template first appeared within the
    // document's own span (first-seen position over lines_observed). >0 means it
    // EMERGED during the window (recurring, count >= 2) rather than being present
    // from the start — the axis that retains a benign template that just started
    // happening (a new Info line) which severity/structure would drop. Self-relative
    // (I3), re-derivable on compose() from merged provenance; NOT "absent from a
    // baseline" (that is consumer-side diff / the pyramid's multi-horizon novelty).
    std::uint32_t novelty{0};
    // Quantized salience score (deterministic, integer; I5). Higher = more
    // salient. (severity ⊕ structural_surprise ⊕ novelty) ⊗ rarity, where severity
    // folds level · failure-lexicon · structural_role.
    std::uint32_t salience{0};
    // §15.4 sub-coordinate (guarantee-2 aid): the reconciled first-seen ordinal of
    // this template within the window (== Bucket::first_seen_index), bounded by the
    // reservoir size. Populated only when a re-derivation coordinate is configured;
    // a reservoir entry is a canon artifact, so locating its raw needs raw-recovery
    // (§15.1-1) then re-canonicalization (§15.1-2). Never a per-line coordinate.
    std::optional<std::uint64_t> within_window_ordinal;
    // §16.6 reservoir→cell cross — LOCATION-only (`level` + `where`-path), read-only,
    // one-way (cube geometry → item location): restores the WHERE of a salient template
    // the (capped) emerging border never surfaced. A PURE FUNCTION of the entry's
    // (dominant level, dominant component); carries NO salience back into the cube and
    // never re-ranks a cell or the border. Populated only when a cube block is emitted
    // (MetaLogConfig::emit_cube). `structural_role` is intentionally left unset — the
    // cross is LOCATION (severity + where), not the full cube cell.
    std::optional<CubeCoord> cube_coord;
};

// Per-node branching statistics (MetaLog SPEC §4.2).
struct BranchingEntry
{
    std::string template_id;
    std::uint64_t fanout{0};
    std::uint64_t total_outgoing{0};
    double entropy_bits{0.0};
};

// Compact "shape of the long tail" summary (SPEC §3.6, MetaLog 0.3).
// Three-field block exposing how concentrated and how loud the tail is,
// without expanding `top_k`. Adds ~60 bytes/window.
//
// * tail_template_count: number of distinct templates strictly below
//   top_k (== StatsBlock::tail_unique; duplicated for spec-conformant
//   self-contained block).
// * tail_entropy_bits: Shannon entropy in bits over the row-normalised
//   tail distribution (p_i = count_i / Σ count_j for j ∈ tail).
//   Collapses toward 0 when one template dominates the tail.
// * tail_max_rate: max(count_i)/lines_observed across the tail.
//   Catches a single emerging template growing inside the tail while
//   never breaching top_k.
//
// All three fields are REQUIRED when the block is present (atomic
// emission). Producers MUST either emit all three or omit the block
// entirely.
struct TailSummary
{
    std::uint64_t tail_template_count{0};
    double tail_entropy_bits{0.0};
    double tail_max_rate{0.0};

    [[nodiscard]] bool operator==(const TailSummary&) const noexcept = default;
};

struct StatsBlock
{
    std::uint64_t unique_templates{0};
    std::vector<TopKEntry> top_k;
    std::size_t top_k_size{0};
    std::uint64_t tail_count{0};
    std::uint64_t tail_unique{0};
    std::optional<double> entropy_bits; // Shannon entropy over full template distribution
    // SPEC §3.6 (MetaLog 0.3). Optional bounded "shape of the tail"
    // signal. Present when there is at least one template in the tail
    // (tail_unique > 0); absent otherwise.
    std::optional<TailSummary> tail_summary;
    // Salience Reservoir (Tier 2). Salient templates retained below top_k.
    // Empty unless MetaLogConfig::reservoir_size > 0. The tail residual excludes
    // these (a promoted template is not double-counted in the omitted mass).
    std::vector<ReservoirEntry> reservoir;
};

// One n-gram row in the behaviour block. `sequence` holds the
// content-derived template_ids in observed order (size == ngram_size).
struct NGramEntry
{
    std::vector<std::string> sequence;
    std::uint64_t count{0};
    double probability{0.0}; // p(last | prefix) — see spec §4
};

struct BehaviorBlock
{
    std::size_t ngram_size{2};
    std::vector<NGramEntry> top_ngrams;
    std::size_t top_ngrams_size{0};
    std::optional<std::uint64_t> graph_edge_count;
    std::optional<std::vector<std::string>> dominant_path; // absent when not computed
    std::optional<std::vector<BranchingEntry>> branching;  // absent when not computed
};

struct StabilityBlock
{
    std::string previous_window_end_iso; // RFC 3339 UTC
    double kl_divergence{0.0};           // KL(current || previous)
    double js_divergence{0.0};           // symmetric Jensen-Shannon, [0, 1] (log base 2)
    std::uint64_t new_templates{0};
    std::uint64_t vanished_templates{0};
    double stability_score{1.0}; // producer-defined; we use 1 - js_divergence
};

struct WindowBlock
{
    std::string start_iso; // RFC 3339 UTC, e.g. "2026-04-24T10:00:00Z"
    std::string end_iso;
    std::uint64_t duration_seconds{0};
    std::uint64_t lines_observed{0};
};

// Override for the REPORTED window bounds at close_window, decoupled from the
// open/close machinery times (insight_determinism_model.md § Event-time, MUST 3).
// A deterministic-batch caller supplies the input's parseable-timestamp envelope
// ([min, max], or the epoch sentinel zero-width when a side has no parseable
// timestamp) so the document's window reflects the log's own event-time span, not
// an arrival/forward-filled time. Live callers omit it (bounds = open/close times).
struct ReportedWindowBounds
{
    Timestamp start;
    Timestamp end;
};

struct ProducerBlock
{
    std::string name{"insight"};
    std::string version{"0.6.0"};
    std::string implementation_uri{"https://github.com/CodeRoasted/insight"};
};

struct SourceBlock
{
    std::optional<std::string> service;
    std::optional<std::string> fleet;
    std::optional<std::uint64_t> host_count;
    std::optional<std::string> host;
    std::map<std::string, std::string> tags;

    [[nodiscard]] bool operator==(const SourceBlock&) const noexcept = default;
};

// Re-derivation coordinate (SPEC §15): makes a window addressable back to its
// source so `raw(window) = replay(source, bounds)` with no raw buffering, and every
// finding is citable/verifiable. DESCRIPTIVE metadata only — bit-identical across
// replays (I5) and MUST NOT feed any deterministic-content / retention / salience
// compute (§15.6). Present on a document only when a `source_ref` is configured.
struct SourceRef
{
    // Selects the resolver (e.g. "logcraft" replay, a CI-artifact kind). Opaque to
    // the spec; a producer MUST NOT assume a particular resolver.
    std::string resolver_kind;
    // Opaque, resolvable handle — meaning defined by the environment (a replay
    // source key, an immutable artifact URI, an otel_trace ref, …).
    std::string handle;
    [[nodiscard]] bool operator==(const SourceRef&) const noexcept = default;
};

struct EventTimeBounds
{
    // The window is [start_tick, end_tick) in EVENT-TIME integer ticks. Integers
    // (no float) and bit-identical across replays (§15.3). Window membership MUST be
    // by event-time only — never the global sequence counter or replay depth.
    std::uint64_t start_tick{0};
    std::uint64_t end_tick{0};
    [[nodiscard]] bool operator==(const EventTimeBounds&) const noexcept = default;
};

struct ReDerivationCoordinate
{
    // §15.2: a coordinate is XOR — either RAW (source_ref + bounds set, children
    // absent) or COMPOSED (children set, source_ref + bounds absent). Sentinel
    // values on composed coordinates are explicitly forbidden by §15.2 (encoding
    // note). Consumers discriminate by the presence of `children`.
    std::optional<SourceRef> source_ref;   // RAW only
    std::optional<EventTimeBounds> bounds; // RAW only
    // Guarantee-2 (fingerprint reproduction) aids — optional (§15.1-2): canon output
    // depends on canon code + config, not just raw bytes. May appear on EITHER kind.
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> config_hash;
    // Composed documents ONLY (§15.5): the non-empty SET of raw (or recursively
    // composed) children's coordinates. A composed coordinate resolves via its
    // children — never via a coarse [first, last] bound (which over-claims across
    // gaps / shards / sources).
    std::optional<std::vector<ReDerivationCoordinate>> children;
    [[nodiscard]] bool operator==(const ReDerivationCoordinate&) const noexcept = default;
};

// Provenance entry recording one input that fed a composed document
// (SPEC §12.4).
struct ProvenanceEntry
{
    std::string window_start_iso;
    std::string window_end_iso;
    SourceBlock source;
    std::uint64_t lines_observed{0};
    std::optional<std::string> document_id;
    // The input's own re-derivation coordinate (§15.5), so a composed document's
    // coordinate resolves to this raw child. Absent when the input had none.
    std::optional<ReDerivationCoordinate> coordinate;
};

struct MetaLogDocument
{
    std::string metalog_version{"0.6.0"};
    ProducerBlock producer{};
    WindowBlock window{};
    SourceBlock source{};
    StatsBlock stats{};
    std::optional<BehaviorBlock> behavior;
    std::optional<StabilityBlock> stability;
    std::map<std::string, std::string> templates;           // optional dedup map (SPEC §3.4)
    std::optional<std::vector<ProvenanceEntry>> provenance; // absent unless composed (SPEC §12.4)
    // Processing-identifier strings (SPEC §2.4). Opaque names of the contract
    // under which the document was produced; gate `compose()` / diff
    // comparability (§13). Set from MetaLogConfig at close_window.
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> retention_profile;
    // Re-derivation coordinate (SPEC §15). Present whenever the producer was
    // configured with a source_ref; a composed document carries `children` instead
    // of addressing a single source. Absent otherwise.
    std::optional<ReDerivationCoordinate> coordinate;
    // Intra-window cube (SPEC §16) — joint categorical condensation. EXPERIMENTAL,
    // additive: present only when the producer opted in (MetaLogConfig::emit_cube).
    // Absent = "no joint-categorical information available". The cube is part of the
    // §2.4 comparability contract (axes frozen per canonicalization_version /
    // retention_profile); two cubes diff into a cube_diff only when their axes match.
    std::optional<CubeBlock> cube;

    // The copy/move/assign/dtor are user-declared here and HAND-WRITTEN (not `= default`) in the
    // module IMPLEMENTATION unit `src/metalog.api.impl.cpp`. RATIONALE — MSVC C++23-modules port:
    // a `= default` special member is always compiler-synthesizable, so under MSVC a consumer module
    // TU (insight.detection) SYNTHESIZES its own copy/move (anywhere they are defaulted — implicit,
    // in-class, or even out-of-line in an impl unit) and, under Release /O2 /Ob2, INLINES + miscompiles
    // the `std::optional<CubeBlock> cube` member: the destination optional is left spuriously ENGAGED
    // over an unconstructed CubeBlock, so a later ~MetaLogDocument frees a garbage std::vector<CubeAxis>
    // (AV 0xc0000005). (An impl-unit `= default` additionally caused LNK2005 — metalog's body AND the
    // consumer's synthesized one collide.) GENUINELY user-provided (hand-written) bodies are NOT
    // synthesizable, so the consumer must emit a real CALL into metalog's single, correctly-compiled
    // definition — the same path the destructor already takes. The bodies are member-wise (== the
    // implicit semantics; determinism goldens unaffected, and metalog's tests catch any dropped member
    // since the serialized output would change); `cube` is built via in_place/emplace to also sidestep
    // optional<CubeBlock>'s own copy/move ctor. Default ctor stays `= default` (the consumer never
    // synthesizes it, and it does not touch the miscompiled optional path). MetaLogDocument is never
    // aggregate-initialized, so user-declaring these breaks no call site.
    MetaLogDocument() = default;
    MetaLogDocument(const MetaLogDocument&);
    MetaLogDocument(MetaLogDocument&&) noexcept;
    MetaLogDocument& operator=(const MetaLogDocument&);
    MetaLogDocument& operator=(MetaLogDocument&&) noexcept;
    ~MetaLogDocument();
};

// ── Producer configuration ─────────────────────────────────────

// Template-string emission mode (SPEC §3.4).
enum class TemplateEmissionMode : std::uint8_t
{
    Inline = 0, // emit `template` inside each top_k entry (back-compat default)
    Dedup = 1,  // emit `templates` top-level map only; no inline strings
    IdOnly = 2, // omit template strings entirely (consumer resolves out-of-band)
};

struct MetaLogConfig
{
    static constexpr std::size_t kDefaultTopKSize = 64;
    static constexpr std::size_t kDefaultTopNgramsSize = 32;
    static constexpr std::size_t kDefaultMaxNgramKeys = 4096;
    static constexpr std::size_t kDefaultTopBranchingSize = 64;
    static constexpr std::size_t kDefaultDominantPathMaxSteps = 8;

    // Max entries kept in stats.top_k; the rest are summarised into
    // tail_count / tail_unique. Default 64 (~10 KB envelope per spec
    // §11). Set to 0 to skip top_k emission entirely (still bounded).
    std::size_t top_k_size{kDefaultTopKSize};

    // Salience Reservoir size M (Tier 2): max templates retained by salience
    // below top_k. 0 = disabled (default — pure frequency retention, pre-Phase-2
    // behaviour). Streaming funds M by shrinking top_k (~64); batch sets it large.
    std::size_t reservoir_size{0};

    // Reservoir diversity cap: max exemplars admitted per "kind"
    // (structural_role × dominant_level), so M optimises COVERAGE of distinct
    // salient kinds over depth — otherwise M fills with variants of one failure
    // (test_query_0/_1/… FAILED) and crowds out a different failure. 0 = no cap.
    std::size_t reservoir_per_kind_cap{0};

    // Order of n-gram emitted in the behaviour block. Spec emits a
    // single order per document. Must be 2 or 3.
    std::size_t ngram_size{2};

    // Max entries kept in behavior.top_ngrams. Default 32.
    // Set to 0 to disable emission of the behavior block entirely.
    std::size_t top_ngrams_size{kDefaultTopNgramsSize};

    // Max distinct n-gram keys retained before bounded dropping kicks
    // in. Once the cap is reached, counts on existing keys keep
    // updating but new keys are dropped. Bounds memory.
    std::size_t max_ngram_keys{kDefaultMaxNgramKeys};

    // When true (default), the engine remembers the previous closed
    // window's template frequencies and emits a stability block on
    // every subsequent window.
    bool emit_stability{true};

    // Template-string emission mode (SPEC §3.4). Default Inline keeps
    // existing 0.1.x consumers happy; switch to Dedup or IdOnly to
    // shrink the envelope.
    TemplateEmissionMode template_emission{TemplateEmissionMode::Inline};

    // Cap on `behavior.branching` entries; 0 disables.
    std::size_t top_branching_size{kDefaultTopBranchingSize};

    // Cap on `behavior.dominant_path` length; 0 disables.
    std::size_t dominant_path_max_steps{kDefaultDominantPathMaxSteps};

    // Reported as producer.version in the envelope.
    std::string producer_version{"0.6.0"};

    // Intra-window cube emission (SPEC §16) — EXPERIMENTAL, additive. false (default)
    // = no cube block, zero overhead on the ingest hot path (one predicted-not-taken
    // branch per ingest_event, same discipline as max_param_histograms). true builds a
    // CLOSED cube over the v0.6.0 reference axes (level × structural_role × where-chain,
    // WHERE grounded in canon `component`) at close_window, plus the §16.6
    // reservoir→cell cross on each reservoir entry. Removable in a single revert
    // (§16.8): the cube is never the source of truth for any 1-D marginal. A producer
    // emitting a cube MUST bump canonicalization_version (the cube joins the §2.4
    // comparability contract) — that bump is the caller's, set via the field above.
    bool emit_cube{false};

    // Re-derivation source (SPEC §15). When set, close_window stamps a coordinate
    // on the document (source_ref + the window's event-time bounds). The engine does
    // NOT derive its own source — the producer/ingest layer sets this (e.g. the
    // LogCraft harness sets {resolver_kind="logcraft", handle=scenario+seed}; a CI
    // run sets {resolver_kind=<artifact-kind>, handle=<artifact URI>}). Unset =
    // no coordinate emitted (the conservative default; e.g. the line-agnostic diff).
    std::optional<SourceRef> source_ref;

    // Opaque processing-identifier strings (SPEC §2.4) — name the CONTRACT the
    // document was produced under. Stamped onto MetaLogDocument at close_window;
    // gate `compose()` / diff comparability (§13). The `canonicalization_version`
    // also serves as the guarantee-2 aid stamped into a re-derivation coordinate
    // (§15.1-2). The `retention_profile` names the retention parameters in effect
    // (top_k size, reservoir admission weights/size/diversity caps, salience
    // arithmetic — §3.1 / §3.7); MUST be bumped when any of those change.
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> retention_profile;

    // Max number of wildcard positions to histogram per top_k entry.
    // 0 = disabled (default — zero overhead on the ingest_event hot path;
    //               one predicted-not-taken branch per ingest call).
    // When N > 0: the first min(N, params.size()) wildcard positions are
    // tracked per template bucket.  Memory bounded by:
    //   top_k_size × max_param_histograms × max_histogram_values map entries.
    std::size_t max_param_histograms{0};

    // Max distinct values tracked per histogram slot per template.
    // Additional distinct values are counted in FieldHistogram::total but
    // not stored individually in value_counts.
    // Default 64 bounds each slot to ~5 KB at typical string sizes.
    static constexpr std::size_t kDefaultMaxHistogramValues{64};
    std::size_t max_histogram_values{kDefaultMaxHistogramValues};

    [[nodiscard]] bool operator==(const MetaLogConfig&) const noexcept = default;
};

// ── Diff document (SPEC §13) ───────────────────────────────────

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
    std::vector<std::vector<std::string>> new_ngrams;
    std::vector<std::vector<std::string>> vanished_ngrams;
    std::vector<NGramRateChange> rate_changed;
};

// Per-(template_id, param_index) JS divergence between the value_counts
// distributions of two consecutive MetaLog windows.
//
// Only populated when both documents were produced with
// MetaLogConfig::max_param_histograms > 0 and the same template_id
// appears in both.
//
// js_divergence uses the same Laplace-smoothed log2 convention as
// MetaLogDiff::js_divergence — value in [0, 1] (bits, clamped).
// entropy_bits fields are the Shannon entropy of each window's
// value_counts map (identical convention as FieldHistogram::entropy_bits).
struct FieldHistogramDelta
{
    std::string template_id;
    std::uint32_t param_index{0};
    double js_divergence{0.0};
    double previous_entropy_bits{0.0};
    double current_entropy_bits{0.0};
    // Per-side observation count backing each distribution (FieldHistogram::total).
    // This is the sample size the JS divergence is estimated from — the basis for
    // a consumer's confidence gate (min-sample floor): a high JS over a handful of
    // observations is sampling noise, not a regime shift. Distinct from cardinality
    // (number of DISTINCT values) and from the template's stream share (population
    // proportion). May exceed sum(value_counts) when high-cardinality values were
    // not retained individually.
    std::uint64_t previous_sample_count{0};
    std::uint64_t current_sample_count{0};
    // Cardinality tracking (SPEC §3.5.2). Zero when either document did not
    // provide approximate_cardinality for this slot.
    std::uint64_t previous_cardinality{0};
    std::uint64_t current_cardinality{0};
    // Signed delta: current_cardinality - previous_cardinality.
    // Positive = more unique values observed (potential injection / bloom).
    std::int64_t cardinality_delta{0};
};

struct DocumentRef
{
    std::string window_start_iso;
    std::string window_end_iso;
    std::optional<std::string> document_id;
};

// Pairwise change in the bounded long-tail shape (SPEC §3.6 tail_summary).
// Present only when BOTH documents carried a tail_summary (both had a non-empty
// tail) — a one-sided tail is a tail appearing/vanishing, which the template-
// level new/vanished signals already express. Mirrors the TailSummary triple as
// before / after / delta (delta = current - previous):
//   * tail_template_count — distinct templates sitting below top_k.
//   * tail_entropy_bits    — tail concentration. A NEGATIVE delta means the tail
//     is collapsing toward one dominant template.
//   * tail_max_rate        — tail loudness. A POSITIVE delta means the loudest
//     tail template is growing relative to the stream.
// "Louder AND more concentrated" (max_rate up, entropy down) is the classic
// emerging-chronic-error signature — a single error growing inside the tail
// without ever breaching top_k. eidos `TailShift` is the streaming analogue;
// this is the same signal as a pairwise, stateless diff field.
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

// ── Cube diff (SPEC §13.6, EXPERIMENTAL) ───────────────────────
//
// The emerging border between two cube blocks: the smallest constraint
// characterising what GREW (and the dual, what vanished) between `previous` and
// `current`. The emerging region (count_prev ≤ θ_was ∧ count_cur ≥ θ_now — two
// ABSOLUTE thresholds, never a ratio, §16.5 MUST-2) is order-convex, bounded by a
// (lower, upper) border pair.

// One border cell: the constraint coordinate + the (was, now) counts it bounds.
struct CubeBorderCell
{
    CubeCoord coord;
    std::uint64_t previous_count{0};
    std::uint64_t current_count{0};
    [[nodiscard]] bool operator==(const CubeBorderCell&) const noexcept = default;
};

// An order-convex region as a (lower, upper) border pair (§13.6):
//  * lower — the most-SPECIFIC emerging cells (the precise description).
//  * upper — the most-GENERAL emerging cells = the minimal generators = the
//    deterministic HEADLINE (computed, not narrated).
struct CubeBorder
{
    std::vector<CubeBorderCell> lower;
    std::vector<CubeBorderCell> upper;
    [[nodiscard]] bool operator==(const CubeBorder&) const noexcept = default;
};

// The cube_diff block: emitted only when BOTH documents carried a cube AND their
// axes are equal (the §2.4 comparability gate + an equal cube schema). `axes`
// equals both inputs' cube axes.
struct CubeDiffBlock
{
    std::vector<CubeAxis> axes;
    std::optional<CubeBorder> emerging;  // growth region
    std::optional<CubeBorder> vanishing; // disappearance region (the dual)
    [[nodiscard]] bool operator==(const CubeDiffBlock&) const noexcept = default;
};

struct MetaLogDiff
{
    std::string diff_version{"0.6.0"};
    DocumentRef previous{};
    DocumentRef current{};
    std::optional<double> kl_divergence;
    std::optional<double> js_divergence;
    std::optional<double> stability_score;
    std::vector<TemplateDelta> template_deltas;
    std::vector<std::string> new_templates;
    std::vector<std::string> vanished_templates;
    std::vector<BranchingDelta> branching_delta;
    std::optional<NGramDelta> ngram_delta;
    // Per-param distribution shift. Empty unless both documents were produced
    // with max_param_histograms > 0 and share at least one template_id.
    // Sorted by js_divergence descending (highest shift first).
    std::vector<FieldHistogramDelta> field_histogram_deltas;
    // Long-tail shape change. Present only when both documents carried a
    // tail_summary. See TailDelta.
    std::optional<TailDelta> tail_delta;
    // Emerging-border cube diff (SPEC §13.6) — EXPERIMENTAL. Present only when both
    // documents carried a `cube` and their axes are equal. Structured evidence (the
    // upper border is the deterministic headline); NOT an alert on its own.
    std::optional<CubeDiffBlock> cube_diff;
};

} // namespace insight::metalog
