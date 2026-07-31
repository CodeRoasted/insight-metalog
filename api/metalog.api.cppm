// insight.metalog.api — public DATA surface (the MetaLog DTOs/config/enums, ADR-3.D4).
// Header-only structs (no impl units) → no api↔detail cycle. std via internal; canon types
// (LogLevel/Timestamp/StructuralRole/CanonicalEvent) via import insight.canon.
export module insight.metalog.api;
import insight.metalog.internal;
import insight.canon;

export namespace insight::metalog
{

// ── Spec envelope (mirrors v0.6.0 schema) ──────────────────────

// Per-template per-wildcard-position value frequency table.
//
// The canon masker maps variable token positions to <*>, making field values invisible
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

// W1 ordinal carrier (§4A.4 SRC-D-W1-2): a per-template, FIELD-keyed binned histogram for a
// declared ordinal field (canon kOrdinalFieldCatalog). Counts over the schedule's frozen log2
// ladder — full tail, NO frequency cap (B is small + fixed, so the carrier is bounded by
// construction; this is exactly the representation the 4A.2 high-card suppression was a stopgap
// for). A distinct stream from the positional, categorical `field_histograms`/`value_counts`: a
// field is ordinal XOR categorical (D-W1-5), so the two never collide. Populated only when
// MetaLogConfig::max_param_histograms > 0 (the batch / full-fidelity value-tracking path); empty
// (omitted on the wire) otherwise → non-ordinal documents stay byte-identical (SRC-D-W1-4).
struct OrdinalHistogram
{
    std::string field_name; // the declared ordinal field (e.g. "latency_ms") — surfaced on the
                            // diff row for `attributable_to` (D-W1-3)
    // the versioned schedule id (the eidos diff comparability key, SRC-D-W1-4)
    std::string schedule_id;
    std::vector<std::uint64_t> counts; // counts[B] over the schedule's log2 ladder
    std::uint64_t total{0};            // Σcounts (the per-field observation count = N for W1)
};

// The W1 ladder (§4A.4 SRC-D-W1-2): map a canonical-unit value onto its log2-octave bin index for
// `schedule`, clamped to [0, B-1]. Pure integer — floor(log2) by a shift loop, no float, no edge
// table; this IS the frozen, versioned ladder (metalog owns binning; eidos is ladder-agnostic at
// w=1). `value` is non-negative (canon's parser rejects negatives); 0 and 1 fall in bin 0.
[[nodiscard]] constexpr std::uint32_t ordinal_bin_index(OrdinalSchedule schedule,
                                                        std::int64_t value) noexcept
{
    const std::uint32_t bins{ordinal_schedule_bins(schedule)};
    if (bins == 0U || value <= 1)
        return 0U;
    std::uint32_t octave{0U};
    for (std::uint64_t magnitude{static_cast<std::uint64_t>(value)}; magnitude > 1U;
         magnitude >>= 1U)
        ++octave;
    return octave < bins ? octave : bins - 1U;
}

// The scale-relative magnitude of a W1 ordinal drift (§4A.4). How far the binned value
// distribution moved along its log2-octave ladder, in coarse octave bands. The octave measure is
// inherently SCALE-RELATIVE — a distance is proportional (400→800 ms and 40→80 ms are both +1
// octave, 100 ms is HIGH for a 10 ms op and NONE for a 2 s op), so the ladder IS the normalization:
// the differential-axis scale-relativity mandate is met WITHOUT any median/IQR divide. NONE means
// within-noise (sub-octave jitter). This bucket is BOTH the eidos OrdinalDrift row's magnitude AND
// the Attribution Cube's first differential axis (latency_shift) — metalog owns the ladder AND the
// distance, one home, consumed by both.
enum class OrdinalShift : std::uint8_t
{
    None = 0, // within-noise (no drift surfaced)
    Low = 1,
    Med = 2,
    High = 3,
};

[[nodiscard]] inline std::string_view to_string(OrdinalShift shift) noexcept
{
    switch (shift)
    {
    case OrdinalShift::None:
        return "none";
    case OrdinalShift::Low:
        return "low";
    case OrdinalShift::Med:
        return "med";
    case OrdinalShift::High:
        return "high";
    }
    return "none";
}

// Which way mass moved along the ladder. Metalog reports the raw, semantics-free direction; the
// eidos diff owns the operator-facing regression/recovery Polarity (Up = higher values = slower /
// larger = worse; Down = faster / smaller = better), and the Attribution Cube's latency_shift axis
// emerges on EITHER direction as a signed, polarity-mute band (up_* / down_*, distinct bands;
// metalog reports the fact, the reading stays in eidos — cube_differential_axes.md §7.4).
enum class OrdinalDriftDirection : std::uint8_t
{
    None = 0, // neutral (no net movement)
    Up = 1,   // mass moved UP the ladder (higher values)
    Down = 2, // mass moved DOWN the ladder (lower values)
};

// A W1 ordinal-drift verdict: the scale-relative shift bucket + the direction mass moved.
struct OrdinalDrift
{
    OrdinalShift shift{OrdinalShift::None};
    OrdinalDriftDirection direction{OrdinalDriftDirection::None};
};

// The exact 1-D Wasserstein-1 (earth-mover) distance between two binned ordinal histograms on the
// SAME schedule (§4A.4 D-W1-1), reduced to a shift bucket + direction. numerator =
// Σ_i |CumA_i·N_b − CumB_i·N_a| (w=1, the log ladder), accumulated in a signed 128-bit integer via
// det::FixedReducer (order-independent, exact, cross-stdlib + MSVC bit-identical); direction =
// sign(Σ_i (CumB_i·N_a − CumA_i·N_b)). The bucket is an EXACT integer cross-multiply against frozen
// octave thresholds θ_k (no float, no division, no float→int — [[det-math-f5-determinism]]), so the
// verdict is replay-stable and golden-frozen. The θ_k are pre-registered (anti-endogamy): ≥5
// octaves → HIGH, ≥2 → MED, ≥0.5 → LOW, below → NONE. A zero total (or empty histogram) is a
// degenerate pairing → {NONE, None}; the caller still gates the schedule-id comparability
// (SRC-D-W1-4).
[[nodiscard]] inline OrdinalDrift ordinal_w1(const std::vector<std::uint64_t>& previous,
                                             const std::vector<std::uint64_t>& current,
                                             std::uint64_t previous_total,
                                             std::uint64_t current_total)
{
    using insight::det::i128;
    using insight::det::u128;

    // Frozen octave thresholds θ_k on W1 = numerator/(Na·Nb). Compared by EXACT integer cross-
    // multiply: the drift reaches level k ⟺ numerator·θden ≥ θnum·Na·Nb. Conservative-biased —
    // a real regime shift must never read NONE. FROZEN from scenario-35's measured numerators.
    constexpr std::int64_t kHighNum{5}, kHighDen{1}; // ≥ 5 octaves → HIGH  (measured pole 9.96)
    constexpr std::int64_t kMedNum{2}, kMedDen{1};   // ≥ 2 octaves → MED   (interior, pinned)
    constexpr std::int64_t kLowNum{1}, kLowDen{2};   // ≥ 0.5 octave → LOW; below → NONE (pole 0.14)

    if (previous_total == 0 || current_total == 0)
        return {}; // degenerate pairing — denom would be 0; no drift is defined

    const i128 total_a{static_cast<i128>(u128{previous_total})};
    const i128 total_b{static_cast<i128>(u128{current_total})};
    insight::det::FixedReducer numerator_reducer; // Σ |CumA·Nb − CumB·Na|
    insight::det::FixedReducer direction_reducer; // Σ (CumB·Na − CumA·Nb)
    std::uint64_t cum_a{0};
    std::uint64_t cum_b{0};
    const std::size_t bins{std::min(previous.size(), current.size())};
    for (std::size_t i{0}; i < bins; ++i)
    {
        cum_a += previous[i];
        cum_b += current[i];
        const i128 a_term{static_cast<i128>(u128{cum_a}) * total_b}; // CumA·Nb
        const i128 b_term{static_cast<i128>(u128{cum_b}) * total_a}; // CumB·Na
        const i128 signed_term{b_term + (-a_term)};                  // CumB·Na − CumA·Nb
        direction_reducer.add_fixed(signed_term);
        numerator_reducer.add_fixed(signed_term >= i128{0} ? signed_term : -signed_term);
    }

    const i128 numerator{numerator_reducer.raw()};
    const i128 denom{total_a * total_b};
    const auto reaches{
        [&](std::int64_t threshold_num, std::int64_t threshold_den)
        { return (numerator * i128{threshold_den}) >= (i128{threshold_num} * denom); }};
    OrdinalDrift drift;
    if (reaches(kHighNum, kHighDen))
        drift.shift = OrdinalShift::High;
    else if (reaches(kMedNum, kMedDen))
        drift.shift = OrdinalShift::Med;
    else if (reaches(kLowNum, kLowDen))
        drift.shift = OrdinalShift::Low;
    // else: within-noise → OrdinalShift::None (the default)

    const i128 direction{direction_reducer.raw()};
    if (!(direction >= i128{0}))
        drift.direction = OrdinalDriftDirection::Up; // mass UP the ladder → slower/larger → worse
    else if (direction >= i128{1})
        drift.direction = OrdinalDriftDirection::Down; // mass DOWN → faster/smaller → better
    // else: perfectly balanced → OrdinalDriftDirection::None (the default)

    return drift;
}

// TemplateRegistry (SRC-D-TIR-5): the single TemplateId -> template_str association, owned OUTSIDE
// the per-window document (the engine owns one; Sift/diff callers own a local one), injected at the
// display seams (serialize / explain). template_str is a pure DISPLAY attribute — never read on the
// decision path — so it does not belong in the entries that flow through the pyramid; this is its
// one home. Append-only, intern-once-per-id (same id => same canon-masked content, so first writer
// wins). Backed by node-stable std::unordered_map, so returned string_views stay valid for the
// registry's lifetime. Every member is defined OUT OF LINE (below), non-inline, ON PURPOSE — never
// fold these back into the class body. `table_` is a std::unordered_map keyed on TemplateId, which
// is exported from insight.canon (a module-attached type). gcc-15 emits the map's out-of-line
// std::_Hashtable members
// (_M_reset, _M_update_bbegin, …) with *internal* linkage for a module-attached key, so an inlined
// copy/move/merge/intern in a *consumer* TU leaves them unresolved at link (surfaces only once the
// consumer lives in a separate link unit — e.g. the insight-playground unit/contract target split).
// Keeping the ops non-inline emits them once into libinsight_metalog; consumers just call the
// external symbol. TemplateRegistry is a pure DISPLAY structure (never on the decision path), so
// the forgone inlining is perf-immaterial. clang is unaffected — this is correctness-preserving
// there.
class TemplateRegistry
{
  public:
    TemplateRegistry();
    TemplateRegistry(const TemplateRegistry&);
    TemplateRegistry(TemplateRegistry&&) noexcept;
    TemplateRegistry& operator=(const TemplateRegistry&);
    TemplateRegistry& operator=(TemplateRegistry&&) noexcept;
    ~TemplateRegistry();

    // Intern (first writer wins); returns a view stable for the registry's lifetime.
    std::string_view intern(TemplateId template_id, std::string_view template_str);
    // The interned string for `template_id`, or "" if unknown.
    [[nodiscard]] std::string_view lookup(TemplateId template_id) const noexcept;
    [[nodiscard]] bool contains(TemplateId template_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;
    // Union another registry in (first writer wins, so an existing id keeps its string — the masker
    // is a pure fn, so a shared id always maps to the same bytes). Used to merge per-shard /
    // per-window registries into one display vocabulary (e.g. sharded pipeline union,
    // baseline∪changed in diff).
    void merge(const TemplateRegistry& other);

  private:
    std::unordered_map<TemplateId, std::string> table_;
};

// Every member is defined OUT OF LINE in the implementation unit src/metalog.api.impl.cpp, NOT here
// in the interface — on purpose, for two compilers at once (see the class note above): gcc-15 needs
// them non-inline (table_'s std::_Hashtable members, module-attached key), and MSVC re-emits an
// out-of-line `= default` special member defined in a module *interface* into every importer
// (LNK2005), so the interface must only DECLARE them. Never fold these back into the interface.

struct TopKEntry
{
    TemplateId
        template_id; // content hash POD (rendered to "h:"+hex at the serialize seam, spec §3.2).
                     // The display template_str lives in the engine-owned TemplateRegistry
                     // (SRC-D-TIR-5 field-drop), resolved by id at the serialize/explain seams.
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<LogLevel> dominant_level;
    // The template's dominant functional source (canon `component` — "src/auth",
    // a build job): the per-template WHERE *label* (sift_where_attribution.md
    // SRC-D-WHERE-2). Populated from dominant_component_of(bucket.component_counts) at
    // build_top_k, independent of the cube, always. EMPTY
    // (disengaged) when the format carried no component — never "" masquerading as
    // a location. Whether it is *surfaced* on a finding is decided window-level off
    // the acquisition block (D-WHERE-6), not by this field's presence.
    std::optional<std::string> dominant_component;
    // Empty unless MetaLogConfig::max_param_histograms > 0.
    std::vector<FieldHistogram> field_histograms;
    // W1 ordinal histograms (§4A.4 SRC-D-W1-2), field-keyed — one per declared ordinal field seen
    // on this template. Empty unless MetaLogConfig::max_param_histograms > 0. Sibling to
    // field_histograms; never collides (a field is ordinal XOR categorical, D-W1-5).
    std::vector<OrdinalHistogram> ordinal_histograms;
};

// Per-window acquisition self-assessment (SPEC §16.x; sift_where_attribution.md
// SRC-D-WHERE-4/D-WHERE-5). Raw, integer STRUCTURAL FACTS about which dimensions a window
// reliably carries — so each consumer (WHERE, the cube axis, the format-relative
// gate) applies its OWN predicate over the same facts ("the window declares its
// own cubeability"), instead of four format checks that drift. NOT a baked verdict.
// A pure function of the frozen ordered window (no float, no float→int, no
// wall-clock); the counts are order-independent → bit-identical cross-stdlib.
// Always present. Carries the window's dimension self-assessment (§6.1.1). Extensible-
// by-addition, emit-gated (a field ships only when its formula is corpus-picked AND a
// consumer needs it): burstiness / mixing_proxy / a convergence readout stay DEFERRED
// (no stubs). The dimension-metadata below (component coverage + the WHERE-tree
// cardinality-per-depth + the joint quantities) feeds the collapse guardrail (§C).
struct AcquisitionBlock
{
    // `component`-axis coverage facts. records_with_component = events that carried
    // a non-empty canon `component`; distinct_components = distinct values seen.
    // lines_observed is NOT duplicated here — read it from WindowBlock.
    std::uint64_t records_with_component{0};
    std::uint64_t distinct_components{0};

    // ── Per-dimension cardinality + dimension-metadata (§6.1.1 / cube_perf_and_collapse §C) ──
    // Raw facts (no verdict). The MANDATORY cardinality signal: the observed distinct-value COUNT
    // per cube dimension {level, where(=distinct_components), role}. This is CARDINALITY, never the
    // count-per-value distribution (that stays the histogram's, F11). Publishing each dimension
    // (not just the ∏ product) is what tells a consumer WHICH dimension is exploding — the input
    // the collapse's LEVEL-vs-WHERE choice and the operator both need. WHERE is the open axis that
    // can explode; level/role are bounded enums (their cardinality is small but still reported).
    std::uint64_t level_cardinality{0}; // distinct log levels observed (the cube's level axis)
    std::uint64_t role_cardinality{0};  // distinct structural_roles observed (the cube's role axis)

    // WHERE-tree distinct-cardinality-per-depth (truncate-and-recount), COARSEST → finest:
    // where_cardinality_per_depth[d] = distinct WHERE prefixes truncated to depth d+1. The
    // WHERE axis is a single depth-1 chain (`[component]`) today, so this is a ONE-element
    // vector == distinct_components; it is shaped per-depth for the forward multi-depth WHERE
    // tree (namespace ▸ service ▸ instance) that the collapse's prefix-truncation coarsens.
    std::vector<std::uint64_t> where_cardinality_per_depth;

    // P_closed — the cube's condensed cell count (the collapse guardrail's budget trigger). Read
    // off the closed cube (built before this block); a pure integer function of the window (§16.9).
    // The combinatorial upper bound ∏|dimᵢ| is DERIVABLE (level × distinct_components × role), so
    // it is not stored — the per-dimension factors above are the richer, non-redundant signal.
    std::uint64_t closed_cells{0};

    // ── O3 span-native acquisition facts (insight_otel_epic.md §13, SRC-D-OTEL-13) — the LICENCE
    // ── Raw, threshold-free integer facts the eidos trace-vocabulary classifiers read to decide
    // whether to speak trace vocabulary (span_records > 0). span_records = span events observed
    // this window; orphan_parent_edges = spans whose declared parent did not resolve to a template
    // in the window (evicted past max_active_spans / straddled the boundary) — counted, never
    // guessed (SRC-D-OTEL-11). Both 0 for a non-span window (additive; a non-OTEL doc is
    // unchanged).
    std::uint64_t span_records{0};
    std::uint64_t orphan_parent_edges{0};
    // O4b Span Links (SRC-D-OTEL-9): declared cross-trace LINK targets that did not resolve to a
    // span in this window (a cross-ROUTE link the aligned-path pooling grain hid, or an external
    // target) — counted, never guessed, SIBLING to orphan_parent_edges (same SRC-D-OTEL-11
    // discipline). The declared-error-model fact: the artifact says how much link topology it could
    // not see, so a consumer (or the streaming stitch, §5.2) tells "no cross-route links" apart
    // from "existed but the segmentation grain hid them". 0 for a window with no unresolved links.
    std::uint64_t orphan_link_edges{0};

    [[nodiscard]] bool operator==(const AcquisitionBlock&) const noexcept = default;
};

// ── O4b service edge (insight_otel_epic.md §13.7.1, SRC-D-OTEL-21)
// ──────────────────────────────────── The one legitimately-cubeable OTEL dimension, DISTILLED: the
// observed span tree projected to (caller_service → callee_service) at COMPONENT granularity
// (bounded by topology², service.name is the low-card WHERE tier), derived at close time in
// resolve_span_edges. NOT a cube Dim (an edge is a per-PAIR fact with no per-event value — a cube
// coordinate would fake a joint); NOT folded into top_ngrams (component-pair vs template-bigram are
// different key spaces). It is its own additive, flag-gated block with its own diff
// (SRC-D-OTEL-21). Deterministic: integer weights, sorted canonical (caller, callee) byte order, no
// float.
struct ServiceEdge
{
    std::string caller;      // caller_component = the PARENT span's service.name
    std::string callee;      // callee_component = the CHILD span's service.name
    std::uint64_t weight{0}; // observed parent→child edges at this component pair this window
    [[nodiscard]] bool operator==(const ServiceEdge&) const noexcept = default;
};

// The window's distilled service topology (SRC-D-OTEL-21). Present iff the window had trace
// substrate (span_records > 0) — a non-span window OMITS the block (absence = *unknown*, not "no
// edges": the edge-block diff requires the block on BOTH sides, D-OTEL-20). Self-edges are excluded
// at derivation (same-component parentage is intra-service, not topology). `edges` is sorted by
// (caller, callee) and bounded to the top `max_service_edges` by weight (canonical-key tie-break);
// `dropped_edges` counts those beyond the cap — the honest truncation fact.
struct ServiceEdgeBlock
{
    std::vector<ServiceEdge> edges;
    std::uint64_t dropped_edges{0};
    [[nodiscard]] bool operator==(const ServiceEdgeBlock&) const noexcept = default;
};

// ── Composed-ruleset identity (II-7, ADR-17) ─────────────────────────────────────────────────────
// The identity of the semantic ruleset that SEGMENTED this document — the comparability key. Rides
// every MetaLogDocument as an ADDITIVE, flag-gated block (the reservoir_delta/AcquisitionBlock
// discipline, [[additive-gated-metalog-block-keeps-wire-version]]): no wire-version bump, and
// ABSENCE = a legacy producer (composed before the ruleset was wired). Two documents are comparable
// (aligned/intent AND template-grain) iff their semantic_identity matches; on mismatch the consumer
// re-segments where raw inputs exist, refuses otherwise — never a silent compare (II-7 verbatim).
struct RulesetPackageRef
{
    std::string name;    // "github"
    std::string version; // "1.0.0"
    [[nodiscard]] bool operator==(const RulesetPackageRef&) const noexcept = default;
};

struct RulesetIdentity
{
    // The composed content hash rendered hex (insight::semantic::ComposedSemantics::identity_hex) —
    // the KEY. A content hash over the actual composed rows, not a hand-bumped label (§4.1).
    std::string semantic_identity;
    // The composed package set, for LEGIBILITY (an operator reads what vocabulary the report
    // understood). The hash is the key; this list is the label. Canonical (package-sorted) order.
    std::vector<RulesetPackageRef> packages;
    [[nodiscard]] bool operator==(const RulesetIdentity&) const noexcept = default;
};

// ── Cube (SPEC §16) ────────────────────────────────────────────
//
// Intra-window joint categorical condensation: a CLOSED cube over a small, fixed
// set of low-card categorical axes (level × structural_role × where-chain). An
// attributor/projector, not a detector — given events already marked interesting
// elsewhere, it answers "what is the smallest conjunction characterising them".
// ALWAYS emitted (1.7.2) and permanently in the canonicalization_version comparability
// contract; its cardinality is bounded by the per-window collapse guardrail (§C). Never
// the source of truth for any 1-D marginal.

// One axis descriptor (§16.2). kind=="categorical" is a flat low-card category
// (cell value = a string); kind=="chain" is a single-parent roll-up hierarchy
// (§16.3) carrying its ordered chain levels (coarsest first) + the frozen
// floor_depth (cell value = an ordered prefix-path array).
struct CubeAxis
{
    std::string name;                              // "level" | "structural_role" | "where"
    std::string kind;                              // "categorical" | "chain"
    std::optional<std::vector<std::string>> chain; // chain only: ordered levels, coarsest first
    std::optional<std::uint32_t> floor_depth;      // chain only: retained depth (≤ len(chain));
                                                   // a WHERE-tree axis coarsened by the collapse
    // guardrail (§C) carries its truncated depth here
    // (< full ⇒ prefix-truncated; 0 ⇒ axis dropped).
    // Per-window collapse depth for an ORDINAL axis (level): the number of lowest-severity
    // levels merged into one band from the bottom (§C3 interval-banding), NEVER across the
    // ERROR/FATAL frontier. Absent / 0 ⇒ no banding. Two cubes compare only at equal collapse
    // (same floor_depth AND band_floor) — the axes carry the collapse so a mismatch is detectable.
    std::optional<std::uint32_t> band_floor;
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
    // Ordinal differential axis, DIFF-TIME ONLY (cube_differential_axes.md §4): the scale-relative,
    // SIGNED, polarity-MUTE latency (DurationLog2Ns) shift band a component's distribution crossed
    // into — "up_low"|"up_med"|"up_high" (higher/slower) or "down_low"|"down_med"|"down_high"
    // (lower/faster). The sign is oriented previous→current (read the MetaLogDiff previous/current
    // stamp): up = current shifted higher than previous. metalog does NOT judge good/bad — the
    // reading layer (eidos) maps up→regression, down→recovery. Present ONLY on a cube_diff
    // emerging-border cell whose component shifted (either direction); ALWAYS absent on a stored
    // cube cell (SHIFT_NONE is the aggregated baseline, never pinned) — the wire object stays open
    // over axis names.
    std::optional<std::string> latency_shift;
    [[nodiscard]] bool operator==(const CubeCoord&) const noexcept = default;
};

// One closed cell: its coordinate + the distributive COUNT measure (integer).
struct CubeCell
{
    CubeCoord coord;
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const CubeCell&) const noexcept = default;
};

// '*' sentinel for a CubeBaseRow's component_id (the empty-component / aggregated-WHERE joint).
// Numeric max, identical to the cube's internal kStar, so a retained base and a freshly-interned
// base agree bit-for-bit.
inline constexpr std::uint32_t kStarComponent{std::numeric_limits<std::uint32_t>::max()};

// One retained interned base joint (DOMAIN-ONLY, never serialised — see CubeBlock::base). The
// per-(level, component, role) observation the closed cube was built from, with `component_id`
// an index into CubeBlock::base_component_dict (kStarComponent = empty component). `level` is
// already cube-normalised (Unknown→Info). Carried so compose()/diff() read the base instead of
// recover_base-ing it per op.
struct CubeBaseRow
{
    LogLevel level{LogLevel::Info};
    std::uint32_t component_id{kStarComponent};
    StructuralRole role{StructuralRole::None};
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const CubeBaseRow&) const noexcept = default;
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

    // DOMAIN-ONLY (in-memory), NEVER serialised — the wire `dto::CubeBlock` (serialize.cpp) omits
    // both, so the wire format is structurally unchanged ("stores closed cells, not base" is a
    // WIRE invariant, not violated). The interned base + its sorted component dictionary the cube
    // was built from, retained so compose_cubes/cube_diff_of read the base directly instead of
    // recover_base-ing it from the closed cells every op (the §13 re-closure perf lever). Immutable
    // after construction (emitted atomically with `cells`). Empty on a cube that did not retain it
    // (e.g. parsed from the wire) → compose/diff fall back to recover_base. A pure cache of the
    // closed cells (their lossless inverse) ⇒ EXCLUDED from operator== (two cubes with equal closed
    // cells are equal whether or not the cache is populated).
    std::vector<CubeBaseRow> base;
    std::vector<std::string> base_component_dict;

    // Identity is the wire-relevant state only; the retained base is a derived cache (above).
    [[nodiscard]] bool operator==(const CubeBlock& other) const noexcept
    {
        return axes == other.axes && cells == other.cells && cell_count == other.cell_count &&
               raw_cell_count == other.raw_cell_count;
    }
};

// The cardinality axes (level, component, role) — index into CubeCardinalityStat::per_axis.
// (Distinct from the axis-descriptor `CubeAxis` above — this is the monitor's per-axis index.)
enum class CardinalityAxis : std::uint8_t
{
    Level = 0,
    Component = 1,
    Role = 2
};

// §13 cardinality monitor (cube_perf_and_collapse.md C2) — the cube's distinct-value counts. A
// PURE function of the closed cube (`cube_cardinality()`), **observability only**: never feeds the
// deterministic content stream. The pre-collapse WARN thresholds (kComponentWarn/kCellsWarn) were
// RETIRED (ADR-18 / studies/005 disposition-D): they predate dimensional collapse and fired on a
// standalone threshold decoupled from the actual collapse trigger (cell_count > kCubeCellBudget).
// The WARN now fires WHEN a collapse is APPLIED (`collapse_note()`, emitted by the eidos pipeline).
// These are the raw counts (the breakdown that annotates that WARN + the acquisition
// cardinalities); kCellsHard remains the documented cube bound (= the cube.cpp collapse budget,
// asserted by tests).
struct CubeCardinalityStat
{
    static constexpr std::size_t kAxisCount{3};
    static constexpr std::uint64_t kCellsHard{
        4096}; // the collapse budget; the cube never exceeds it

    std::uint64_t cells{0};                           // closed cell count
    std::array<std::uint32_t, kAxisCount> per_axis{}; // distinct [level, component, role]
};

// Salience Reservoir entry (Tier 2). A template
// retained by intrinsic SALIENCE rather than frequency — where a rare-but-severe
// event (a lone fatal) survives the bounded fingerprint instead of collapsing
// into the tail. Self-describing: carries why it was kept (salience + the inputs)
// so a consumer/explainer can attribute it. Disjoint from top_k (a template here
// did NOT make top_k by frequency); excluded from the tail residual.
struct ReservoirEntry
{
    // display template_str resolved by id from the engine TemplateRegistry at the
    // serialize/explain seams (SRC-D-TIR-5 field-drop), like TopKEntry.
    TemplateId template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<LogLevel> dominant_level;
    // The template's dominant functional source (canon `component`) — the WHERE
    // *label* (SRC-D-WHERE-2), mirroring TopKEntry. Populated independent of the cube,
    // always; EMPTY when the format carried no component. Distinct from `cube_coord`
    // (the §16.6 LOCATION cross): this is the cube-independent carrier the Sift WHERE
    // rides.
    std::optional<std::string> dominant_component;
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
    // never re-ranks a cell or the border. Always populated (the cube is always built).
    // `structural_role` is intentionally left unset — the
    // cross is LOCATION (severity + where), not the full cube cell.
    std::optional<CubeCoord> cube_coord;
};

// Per-node branching statistics (MetaLog SPEC §4.2).
struct BranchingEntry
{
    TemplateId template_id;
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
    std::vector<TemplateId> sequence;
    std::uint64_t count{0};
    double probability{0.0}; // p(last | prefix) — see spec §4
};

struct BehaviorBlock
{
    std::size_t ngram_size{2};
    std::vector<NGramEntry> top_ngrams;
    std::size_t top_ngrams_size{0};
    std::optional<std::uint64_t> graph_edge_count;
    std::optional<std::vector<TemplateId>> dominant_path; // absent when not computed
    std::optional<std::vector<BranchingEntry>> branching; // absent when not computed
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
// open/close machinery times (bibles/determinism_model.md §7).
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

// Template-string emission mode (SPEC §3.4). Defined before MetaLogDocument so the document can
// carry

struct MetaLogDocument
{
    std::string metalog_version{"0.6.0"};
    ProducerBlock producer{};
    WindowBlock window{};
    SourceBlock source{};
    StatsBlock stats{};
    std::optional<BehaviorBlock> behavior;
    std::optional<StabilityBlock> stability;
    // SRC-D-TIR-5 field-drop: the display strings live in the engine-owned TemplateRegistry,
    // resolved by id at the serialize/explain seams. This producer emits SPEC §3.4's INLINE mode
    // only — the three modes are a producer MAY, and the dedup/id-only arms were never wired
    // (ADR-9).
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
    // Intra-window cube (SPEC §16) — joint categorical condensation. ALWAYS built for a
    // raw window (has_cube = true), collapse-bounded (§C). has_cube survives as the
    // representation's presence flag: a COMPOSED document clears it only on an axis
    // mismatch (!has_cube = "no comparable joint available"). The cube is part of the
    // §2.4 comparability contract (axes frozen per canonicalization_version /
    // retention_profile); two cubes diff into a cube_diff only when their axes match.
    //
    // Representation: an explicit presence flag + inline value, NOT std::optional<CubeBlock>.
    // MSVC /O2 /Ob2 miscompiles the SYNTHESIZED optional<CubeBlock> copy in consumer module TUs
    // (a triviality-propagation bug — it memcpy's a stale _Has_value byte, spuriously engaging a
    // disengaged cube). A presence-bool + inline value keeps MetaLogDocument a copyable value with
    // DEFAULTED special members and honest triviality (bool truly trivial, CubeBlock truly not),
    // so the synthesized copy is always correct. See [[msvc-port-stdlib-isms]].
    bool has_cube{false};
    CubeBlock cube{};
    // Per-window acquisition self-assessment (sift_where_attribution.md SRC-D-WHERE-4):
    // the window's structural facts seeding the WHERE disposition + the cube-dimension
    // self-assessment (§6.1.1, feeds the collapse guardrail). Always present.
    // All-integer, so std::optional is sound here (the
    // bool+inline workaround the cube needs is for vector-owning optionals copied in
    // consumer module TUs; AcquisitionBlock is trivially copyable).
    std::optional<AcquisitionBlock> acquisition;
    // O4b distilled service topology (insight_otel_epic.md §13.7.1, SRC-D-OTEL-21). Present iff the
    // window had trace substrate (span_records > 0); ABSENT for a non-span window (absence =
    // unknown, the additive-block discipline — the edge diff needs the block on both sides). Owns a
    // vector but is stamped once at close and only read (never a synthesized-optional copy on the
    // MSVC /O2 hot path), so std::optional is sound — the RulesetIdentity precedent,
    // [[msvc-port-stdlib-isms]].
    std::optional<ServiceEdgeBlock> service_edges;
    // Composed-ruleset identity (II-7, ADR-17): the semantic_identity + package list of the
    // ruleset that segmented this document. ABSENT = legacy producer (pre-ruleset). Stamped by the
    // producer from the ComposedSemantics that tokenized the input. RulesetIdentity owns a vector,
    // so it follows the CubeBlock precedent risk — but it is stamped once at close and only read
    // (never a synthesized-optional copy on the MSVC /O2 hot path), so std::optional is sound.
    std::optional<RulesetIdentity> ruleset;
    // The run's terminal verdict (ADR-17): one four-class scalar per run, resolved by the
    // D-OUT-RUN-1 precedence (authoritative side-input → console tail → Unknown) and stamped by the
    // producing orchestration on a WHOLE-RUN document. Additive, NO wire-version bump: Unknown is
    // the default AND the wire absence (the serializer omits it) — a legacy/verdict-free document
    // reads back identical ([[additive-gated-metalog-block-keeps-wire-version]]). NOT a cube
    // dimension (OUTCOME is the run LABEL — [[cube-dimension-model]]); a per-quantum slice document
    // (the aligned diff's per-pair windows) correctly keeps Unknown — a quantum is not a run.
    insight::RunOutcome run_outcome{insight::RunOutcome::Unknown};
};

// ── Producer configuration ─────────────────────────────────────

struct MetaLogConfig
{
    static constexpr std::size_t kDefaultTopKSize = 64;
    static constexpr std::size_t kDefaultTopNgramsSize = 32;
    static constexpr std::size_t kDefaultMaxNgramKeys = 4096;
    static constexpr std::size_t kDefaultTopBranchingSize = 64;
    static constexpr std::size_t kDefaultDominantPathMaxSteps = 8;
    static constexpr std::size_t kDefaultMaxActiveTraces = 4096;
    // O3 span_id→template bound (SRC-D-OTEL-11)
    static constexpr std::size_t kDefaultMaxActiveSpans = 16384;
    // O4b service_edges emit cap (SRC-D-OTEL-21)
    static constexpr std::size_t kDefaultMaxServiceEdges = 4096;

    // Max entries kept in stats.top_k; the rest are summarised into
    // tail_count / tail_unique. Default 64 (~10 KB envelope per spec
    // ADR-3.D4). Set to 0 to skip top_k emission entirely (still bounded).
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

    // Error-class retention RESERVE (D-RNK-2 §5.2): a bounded floor of the M slots
    // held exclusively for error-class templates (dominant_level ∈ {Error, Fatal} or
    // role Terminator — the verdict-anchored-failure signal at the metalog layer), so
    // non-failure salience (novelty / structural-surprise) can NEVER evict a real
    // failure from a high-cardinality window. The reserve is admitted by salience then
    // template_id and is EXEMPT from the per-kind cap (its purpose is failure DEPTH —
    // a genuine failure storm keeps its top by salience; the per-kind cap governs only
    // the general pool's diversity). 0 = disabled (default — no reserve). Clamped to
    // reservoir_size. Schema-neutral: a retention policy, not template identity → no
    // canonicalization_version / wire-version bump.
    std::size_t reservoir_error_reserve{0};

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

    // O2 trace-scoping master switch (insight_otel_epic.md O2). Default true: an OTEL event
    // forms its n-gram WITHIN its trace. false is the CONTROL ARM — even OTEL events fall back
    // to the global ring, reproducing the polluted global-order graph on the SAME input (the
    // config mirror of the unit gate's with_trace=false arm; the scenario signal
    // trace_scoping_disabled_control sets it). Non-OTEL ingest is byte-identical either way
    // (no trace context → the global ring is taken regardless), so the flag is OTEL-only.
    bool trace_scoping_enabled{true};

    // O2 trace-scoping (insight_otel_epic.md SRC-D-OTEL-1, OR3): max concurrent OTEL traces whose
    // n-gram ring is held at once. A ring is just the last 1–2 template ids — NOT a per-trace
    // sub-fingerprint. On overflow the oldest-inserted trace's ring is evicted (deterministic
    // FIFO), losing at most one cross-record edge for that trace, never its membership. Bounds
    // per-window state at O(active traces), not O(traces). Consulted ONLY for OTEL inputs
    // (events carrying a trace_id); non-OTEL ingest uses the single global ring at zero cost.
    std::size_t max_active_traces{kDefaultMaxActiveTraces};

    // O3 observed-DAG (insight_otel_epic.md §13, SRC-D-OTEL-11): max span_id → template entries
    // held in a window for close-time parent-edge resolution. A span's declared parent is resolved
    // to an observed edge template(parent)→template(child) at close; a parent evicted past this
    // bound (or outside the window) yields no edge + one `orphan_parent_edges` fact (counted, never
    // guessed). Deterministic FIFO eviction of the oldest-inserted span. Bounds per-window span
    // state at O(active spans). Consulted ONLY for span inputs (records with is_span); 0 disables
    // the bound.
    std::size_t max_active_spans{kDefaultMaxActiveSpans};

    // O4b service-topology emit cap (SRC-D-OTEL-21): max service_edges emitted in the block; edges
    // beyond this (top-weight-K, canonical-key tie-break) fold into `dropped_edges`. The
    // accumulator is bounded by topology² (service.name is low-card); this is the safety cap on the
    // emitted wire block.
    std::size_t max_service_edges{kDefaultMaxServiceEdges};

    // When true (default), the engine remembers the previous closed
    // window's template frequencies and emits a stability block on
    // every subsequent window.
    bool emit_stability{true};

    // Cap on `behavior.branching` entries; 0 disables.
    std::size_t top_branching_size{kDefaultTopBranchingSize};

    // Cap on `behavior.dominant_path` length; 0 disables.
    std::size_t dominant_path_max_steps{kDefaultDominantPathMaxSteps};

    // Reported as producer.version in the envelope.
    std::string producer_version{"0.6.0"};

    // NOTE (1.7.2): the cube (SPEC §16), the per-template `dominant_component` WHERE
    // leaf (SRC-D-WHERE-2), and the per-window `acquisition` block are ALWAYS emitted — the
    // former `emit_cube`/`emit_where` opt-in gates are removed. The cube is permanently
    // in the §2.4 `canonicalization_version` comparability contract, and its cardinality
    // is bounded by the per-window dimensional-collapse guardrail (cube_perf_and_collapse.md
    // §C). The acquisition block carries the window's dimension self-assessment (raw facts;
    // the admissibility verdict is a shared consumer-side predicate, never a wire mask).

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
    //
    // canonicalization_version DEFAULTS to the canon-owned constant (SRC-D-TID-16): the
    // masking rules and the version that names them live together in canon, so a
    // producer cannot silently leave old and new metalogs falsely comparable. Override
    // only to express a different canonicalization contract (e.g. a test fixture).
    std::optional<std::string> canonicalization_version{
        std::string{insight::kCanonicalizationVersion}};
    std::optional<std::string> retention_profile;
    // II-7 composed-ruleset identity (ADR-17): the semantic_identity + package list of the
    // composition that tokenized the input, injected by the producing binary (the engine pipeline
    // sets it from insight::engine::composed_semantics()). DEFAULT unset — a producer that does not
    // inject it emits no ruleset block (a legacy producer; absence-tolerant on the consumer).
    // Unlike canonicalization_version there is no canon-owned default: canon ships no default
    // composition (ADR-17), so the binary that declares its package set is the only one that
    // knows the hash.
    std::optional<RulesetIdentity> ruleset;

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
    TemplateId template_id;
    std::uint64_t previous_count{0};
    std::uint64_t current_count{0};
    std::int64_t delta{0};
    std::optional<double> previous_frequency;
    std::optional<double> current_frequency;
};

struct BranchingDelta
{
    TemplateId template_id;
    double previous_entropy_bits{0.0};
    double current_entropy_bits{0.0};
    double delta_bits{0.0};
};

struct NGramRateChange
{
    std::vector<TemplateId> sequence;
    double previous_probability{0.0};
    double current_probability{0.0};
    double delta{0.0};
};

struct NGramDelta
{
    std::size_t ngram_size{2};
    std::vector<std::vector<TemplateId>> new_ngrams;
    std::vector<std::vector<TemplateId>> vanished_ngrams;
    std::vector<NGramRateChange> rate_changed;
};

// A service edge present on BOTH sides whose observed weight moved (SRC-D-OTEL-21).
struct ServiceEdgeWeightChange
{
    std::string caller;
    std::string callee;
    std::uint64_t previous_weight{0};
    std::uint64_t current_weight{0};
    std::int64_t delta{0}; // current - previous
};

// The service-topology delta (SRC-D-OTEL-21): its OWN diff pass over the two windows' service_edges
// blocks. `emerged`/`vanished` are the appeared-from-nothing / disappeared edge sets at the cube's
// absolute emergence discipline (θ_was=0, θ_now=1). Semantics-free integer/set arithmetic — metalog
// stays polarity-blind (the degraded reading + the fold are eidos, SRC-D-OTEL-22). Present (in
// MetaLogDiff) ONLY when BOTH documents carried a service_edges block; absent ⇒ edge verdicts are
// *unknown* (never "all emerged"). Edges carry the changed-side (emerged) / baseline-side
// (vanished) weight.
struct ServiceEdgeDelta
{
    std::vector<ServiceEdge> emerged;
    std::vector<ServiceEdge> vanished;
    std::vector<ServiceEdgeWeightChange> weight_changed;
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
    TemplateId template_id;
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

// Per-(template_id, ordinal field) pairing of two windows' binned ordinal histograms (§4A.4
// D-W1-1/SRC-D-W1-4 — the W1 channel). Carries BOTH sides' raw counts + totals +
// schedule_ids; the eidos
// diff checks the schedule_ids match (the comparability gate, SRC-D-W1-4, like
// canonicalization_version at diff.cpp) then computes the exact-integer 1-D Wasserstein-1
// earth-mover distance, its direction, and the {field}_shift bucket — metalog carries the counts,
// eidos owns the distance (it is ladder-agnostic at w=1). Only populated when the same
// (template_id, field_name) appears in BOTH documents' ordinal_histograms.
struct OrdinalHistogramDelta
{
    TemplateId template_id;
    std::string field_name;
    std::string previous_schedule_id;
    std::string current_schedule_id;
    std::vector<std::uint64_t> previous_counts;
    std::vector<std::uint64_t> current_counts;
    std::uint64_t previous_total{0};
    std::uint64_t current_total{0};
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
    // Presence-bool + inline value, NOT std::optional<CubeBorder> (CubeBorder owns vectors, so a
    // synthesized optional<CubeBorder> copy hits the same MSVC bug as the cube). Since
    // CubeDiffBlock is itself an inline value member of MetaLogDiff now, these are copied on every
    // MetaLogDiff copy (detection holds std::optional<MetaLogDiff>) even when empty — bool+value
    // keeps that copy sound.
    bool has_emerging{false};
    CubeBorder emerging{}; // growth region (valid iff has_emerging)
    bool has_vanishing{false};
    CubeBorder vanishing{}; // disappearance region, the dual (valid iff has_vanishing)
    [[nodiscard]] bool operator==(const CubeDiffBlock&) const noexcept = default;
};

// ── reservoir delta (§5.3) — the streaming chronic-vs-new consumption seam ──
// Direction of an ERROR/FATAL failure-frontier crossing, oriented previous→current
// (the MetaLogDiff previous/current stamp). SIGNED but POLARITY-MUTE: metalog records
// which way a template's dominant_level moved across the failure frontier; the
// escalation (up) / recovery (down) READING is the consumer's — the latency_shift
// discipline (cube_differential_axes.md §7.4). Never a good/bad verdict here.
enum class FrontierDirection : std::uint8_t
{
    Up,  // crossed INTO the failure band (…→ Error/Fatal)
    Down // crossed OUT of the failure band (Error/Fatal →…)
};

// One rare-salient template on the reservoir-delta membership boundary. The snapshot
// carried is the entry as it stands on the side that OWNS it: the current-window entry
// for new_salient, the previous-window entry for vanished_salient. Both draw from a
// document's RESERVOIR (which carries salience + structural_role), so all five fields
// are populated.
struct ReservoirDeltaEntry
{
    TemplateId template_id;
    std::optional<LogLevel> dominant_level;
    StructuralRole structural_role{StructuralRole::None};
    std::uint32_t salience{0};
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const ReservoirDeltaEntry&) const noexcept = default;
};

// One failure-frontier crossing: a template present in BOTH sides' salience memory
// whose dominant_level crosses the ERROR/FATAL frontier. Carries both levels so the
// consumer can attribute the crossing without re-reading the documents.
struct FrontierCrossing
{
    TemplateId template_id;
    FrontierDirection direction;
    std::optional<LogLevel> previous_level;
    std::optional<LogLevel> current_level;
    [[nodiscard]] bool operator==(const FrontierCrossing&) const noexcept = default;
};

// The §5.3 reservoir delta over the two documents' salience memory (top_k ∪ reservoir):
//   * new_salient      — in current.reservoir, absent from previous.(top_k ∪ reservoir).
//   * vanished_salient — in previous.reservoir, absent from current.(top_k ∪ reservoir).
//   * frontier_crossings — on BOTH sides' memory, dominant_level crossing the failure frontier.
// Every list is keyed and sorted by template_id (the canonical key; TemplateId's defaulted
// byte-order). Set-difference + integer level compares over membership that is already
// deterministic content (F5-M8) — no unordered iteration order may leak into any output.
// The reading discipline (who reads which member) is the consumer's, not the producer's:
// new_salient + frontier_crossings are the STREAMING members (read on the anchored
// per-scale diffs); vanished_salient is the BATCH member — a streaming consumer MUST NOT
// alert on it (rarity IS intermittency; §5.3).
struct ReservoirDelta
{
    std::vector<ReservoirDeltaEntry> new_salient;
    std::vector<ReservoirDeltaEntry> vanished_salient;
    std::vector<FrontierCrossing> frontier_crossings;
    [[nodiscard]] bool empty() const noexcept
    {
        return new_salient.empty() && vanished_salient.empty() && frontier_crossings.empty();
    }
    [[nodiscard]] bool operator==(const ReservoirDelta&) const noexcept = default;
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
    std::vector<TemplateId> new_templates;
    std::vector<TemplateId> vanished_templates;
    std::vector<BranchingDelta> branching_delta;
    std::optional<NGramDelta> ngram_delta;
    // O4b service-topology delta (SRC-D-OTEL-21). Present ONLY when both documents carried a
    // service_edges block (absent ⇒ *unknown*). Additive on the DERIVED diff → no diff_version bump
    // (the ngram_delta / latency_shift derived-not-compared precedent). See ServiceEdgeDelta.
    std::optional<ServiceEdgeDelta> service_edge_delta;
    // Per-param distribution shift. Empty unless both documents were produced
    // with max_param_histograms > 0 and share at least one template_id.
    // Sorted by js_divergence descending (highest shift first).
    std::vector<FieldHistogramDelta> field_histogram_deltas;
    // W1 ordinal distribution drift (§4A.4). Empty unless both documents were produced with
    // max_param_histograms > 0 and share a (template_id, declared-ordinal field). Carries both
    // sides' binned counts; the eidos diff computes the Wasserstein-1 distance. Sorted by
    // (template_id, field_name). See OrdinalHistogramDelta.
    std::vector<OrdinalHistogramDelta> ordinal_histogram_deltas;
    // Long-tail shape change. Present only when both documents carried a
    // tail_summary. See TailDelta.
    std::optional<TailDelta> tail_delta;
    // Emerging-border cube diff (SPEC §13.6) — EXPERIMENTAL. Present (has_cube_diff) only when
    // both documents carried a `cube` and their axes are equal. Structured evidence (the upper
    // border is the deterministic headline); NOT an alert on its own. Presence-bool + inline value,
    // NOT std::optional<CubeDiffBlock> — same MSVC consumer-synthesis reason as
    // MetaLogDocument::cube.
    bool has_cube_diff{false};
    CubeDiffBlock cube_diff{};
    // §5.3 reservoir delta. Additive on the DERIVED diff → NO diff_version /
    // canonicalization_version bump (the latency_shift derived-not-compared precedent,
    // [[additive-gated-metalog-block-keeps-wire-version]]). Inline value, NOT a
    // presence-bool pair and NOT std::optional: emptiness IS absence here (all three
    // lists empty ⇒ the block is omitted from JSON — reservoir_delta.empty()), so a
    // separate has_ flag would be redundant state to keep in sync. Inline (not
    // std::optional<ReservoirDelta>) also sidesteps the MSVC consumer-synthesis bug that
    // drove cube_diff to bool+value — detection holds std::optional<MetaLogDiff>, but an
    // inline vector-owning member copies soundly where a synthesized optional would not.
    ReservoirDelta reservoir_delta{};
};

} // namespace insight::metalog
