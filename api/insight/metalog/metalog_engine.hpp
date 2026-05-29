#pragma once

// MetaLog v0.5.0 spec-conformant producer.
//
// Spec: https://github.com/CodeRoasted/metalog-spec (tag v0.5.0).
//
// v0.5.0 additions over 0.1.x:
//   * top-level `templates` dedup map (SPEC §3.4).
//   * behaviour fields `dominant_path`, `branching`, `graph_edge_count`
//     (SPEC §4).
//   * free functions `compose(a, b)` (SPEC §12) and `diff(prev, cur)`
//     producing a `MetaLogDiff` (SPEC §13).
//   * `template` is OPTIONAL inside `top_k` entries; producers may
//     emit it inline, in the top-level `templates` map, or omit it
//     entirely (id-only).
//
// This module turns a stream of CanonicalEvents into a bounded-size
// statistical fingerprint (a MetaLog document). It is the
// **compression** stage of the pipeline (Phase 3 in
// technical_docs/overview/architecture.md): one window in -> one bounded
// fingerprint out. The fingerprint is intentionally lossy; the
// invariant the lossiness has to preserve is that a LogCraft-injected
// incident remains detectable by Phase 4 against the produced
// MetaLog stream.
//
// Implements three of the four spec blocks:
//
//   * stats     (required) — top-K templates + tail summary +
//                            optional Shannon entropy_bits.
//   * behavior  (optional) — top-N n-grams of template_ids at a
//                            single configured order (default 2 =
//                            bigrams), with per-row p(next | prev).
//   * stability (optional) — divergence vs the previous window
//                            (KL, JS, new/vanished templates,
//                            stability_score). First window in a
//                            session emits no stability block.
//
// `attribution` (spec §6) is **reserved for v1.0 of the spec** —
// the sketch wire format is not yet pinned down; emitting it now
// would not interoperate. We therefore do not produce that block.
//
// Lifecycle:
//
//     MetaLogEngine engine{{.top_k_size = 64, .top_ngrams_size = 32}};
//     engine.open_window(window_start);
//     for (const auto& ev : events)
//         engine.ingest_event(ev);
//     auto doc{engine.close_window(window_end)};  // first window: no stability
//     auto json = to_json(doc);
//
//     engine.open_window(next_start);
//     for (...) engine.ingest_event(...);
//     auto doc2{engine.close_window(next_end)};   // doc2.stability populated

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insight/core/types.hpp"
#include "insight/tokenization/canonical_event.hpp"

namespace insight::metalog
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

// Salience Reservoir entry (Tier 2, Salience epic §5 L1 / flaw F1). A template
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
    // Salience Reservoir (Tier 2, F1). Salient templates retained below top_k.
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
    std::optional<std::vector<std::string>> dominant_path;  // absent when not computed
    std::optional<std::vector<BranchingEntry>> branching;   // absent when not computed
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

struct ProducerBlock
{
    std::string name{"insight"};
    std::string version{"0.5.0"};
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
    std::optional<SourceRef> source_ref;       // RAW only
    std::optional<EventTimeBounds> bounds;     // RAW only
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
    std::string metalog_version{"0.5.0"};
    ProducerBlock producer{};
    WindowBlock window{};
    SourceBlock source{};
    StatsBlock stats{};
    std::optional<BehaviorBlock> behavior;
    std::optional<StabilityBlock> stability;
    std::map<std::string, std::string> templates; // optional dedup map (SPEC §3.4)
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

    // Salience Reservoir size M (Tier 2, F1): max templates retained by salience
    // below top_k. 0 = disabled (default — pure frequency retention, pre-Phase-2
    // behaviour). Streaming funds M by shrinking top_k (~64); batch sets it large.
    std::size_t reservoir_size{0};

    // Reservoir diversity cap (F10): max exemplars admitted per "kind"
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
    std::string producer_version{"0.5.0"};

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

struct MetaLogDiff
{
    std::string diff_version{"0.4.0"};
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
};

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
    [[nodiscard]] MetaLogDocument close_window(Timestamp end);

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
        // Announced structural roles seen for this template (F12 → salience).
        // Dominant role feeds the salience severity signal (Terminator = severe).
        std::unordered_map<StructuralRole, std::uint64_t> role_counts;
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
