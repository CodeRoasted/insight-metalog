#pragma once

// MetaLog v0.2.0 spec-conformant producer.
//
// Spec: https://github.com/coderoast-dev/metalog-spec (tag v0.2.0).
//
// v0.2.0 additions over 0.1.x:
//   * top-level `templates` dedup map (SPEC §3.4).
//   * behaviour fields `dominant_path`, `branching`,
//     `sessions_observed`, `session_aware`, `graph_edge_count`
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
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "insight/core/types.hpp"
#include "insight/tokenization/canonical_event.hpp"

namespace insight::metalog
{

// ── Spec envelope (mirrors v0.2.0 schema) ──────────────────────

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

// Per-node branching statistics (MetaLog SPEC §4.2).
struct BranchingEntry
{
    std::string template_id;
    std::uint64_t fanout{0};
    std::uint64_t total_outgoing{0};
    double entropy_bits{0.0};
};

struct StatsBlock
{
    std::uint64_t unique_templates{0};
    std::vector<TopKEntry> top_k;
    std::size_t top_k_size{0};
    std::uint64_t tail_count{0};
    std::uint64_t tail_unique{0};
    std::optional<double> entropy_bits; // Shannon entropy over full template distribution
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
    std::vector<std::string> dominant_path; // empty when not computed
    std::vector<BranchingEntry> branching;  // empty when not computed
    std::optional<std::uint64_t> sessions_observed;
    bool session_aware{false};
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
    std::string version{"0.2.0"};
    std::string implementation_uri{"https://github.com/coderoast-dev/insight"};
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

// Provenance entry recording one input that fed a composed document
// (SPEC §12.4).
struct ProvenanceEntry
{
    std::string window_start_iso;
    std::string window_end_iso;
    SourceBlock source;
    std::uint64_t lines_observed{0};
    std::optional<std::string> document_id;
};

struct MetaLogDocument
{
    std::string metalog_version{"0.2.0"};
    ProducerBlock producer{};
    WindowBlock window{};
    SourceBlock source{};
    StatsBlock stats{};
    std::optional<BehaviorBlock> behavior;
    std::optional<StabilityBlock> stability;
    std::map<std::string, std::string> templates; // optional dedup map (SPEC §3.4)
    std::vector<ProvenanceEntry> provenance;      // empty unless composed (SPEC §12.4)
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
    std::string producer_version{"0.2.0"};

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

struct MetaLogDiff
{
    std::string diff_version{"0.2.0"};
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
        std::unordered_map<LogLevel, std::uint64_t> level_counts;
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

    // Set of distinct session keys observed in the current window.
    // Hot path adds at most one insert per event when `event.session_key != 0`.
    // When all events have session_key == 0 (the default for tokenizers
    // that haven't opted in to MetaLog SPEC §14), this set stays empty
    // and the cost is one predicted-not-taken branch per ingest.
    std::unordered_set<SessionID> sessions_seen_;

    // HyperLogLog sketches for approximate cardinality per (template, param_index).
    // Pimpl to avoid exposing HLL internals in the public header.
    // Defined in metalog_engine.cpp; reset at open_window(), snapshotted at close_window().
    struct HllState;
    std::unique_ptr<HllState> hll_state_;
};

// Free serialiser. Produces a JSON value that conforms to the v0.2.0
// MetaLog envelope.
[[nodiscard]] nlohmann::json to_json(const MetaLogDocument& doc);

// Free serialiser for the diff document (SPEC §13).
[[nodiscard]] nlohmann::json to_json(const MetaLogDiff& diff);

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
