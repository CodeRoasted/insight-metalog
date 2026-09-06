// refs: ADR-3.D4
export module insight.metalog.api;
import insight.metalog.internal;
import insight.canon;

export namespace insight::metalog
{

// invariant: one row per wildcarded parameter position of one template, over the window.
// invariant: populated only when MetaLogConfig::max_param_histograms > 0; empty otherwise.
// refs: F-SRC-metalog-spec:SPEC.md
struct FieldHistogram
{
    // invariant: 0-based wildcard position in CanonicalEvent::params.
    std::uint32_t param_index{0};
    std::unordered_map<std::string, std::uint64_t> value_counts;
    // invariant: every event that matched the template; exceeds the sum of value_counts once
    // max_histogram_values bound the retained values.
    std::uint64_t total{0};
    double entropy_bits{0.0};
    // invariant: an approximate distinct-value count for this slot, never bound by
    // max_histogram_values.
    // invariant: 0 when the producer computed no cardinality for the slot.
    std::uint64_t approximate_cardinality{0};
};

// invariant: counts over the schedule's frozen log2 ladder, full tail and no frequency cap.
// invariant: populated only when MetaLogConfig::max_param_histograms > 0; empty otherwise.
// invariant: a field is ordinal XOR categorical, so this never collides with field_histograms.
// refs: SRC-D-W1-2, SRC-D-W1-4, SRC-D-W1-5
struct OrdinalHistogram
{
    // invariant: the declared ordinal field, surfaced on the diff row for attributable_to.
    // refs: SRC-D-W1-3
    std::string field_name;
    // invariant: the versioned schedule id, and the eidos diff's comparability key.
    // refs: SRC-D-W1-4
    std::string schedule_id;
    // invariant: one count per bin of the schedule's log2 ladder.
    std::vector<std::uint64_t> counts;
    // invariant: the sum of counts, which is the per-field observation count.
    std::uint64_t total{0};
};

// pre: value is non-negative -- canon's parser rejects negatives.
// post: the octave index of value, clamped to [0, bins-1]; 0 and 1 both fall in bin 0.
// invariant: pure integer, floor(log2) by a shift loop -- no float and no edge table.
// refs: SRC-D-W1-2
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

// invariant: octave bands, so the measure is scale-relative and needs no median/IQR divide.
// invariant: None is within-noise (sub-octave jitter), never absence of data.
// refs: SRC-D-W1-2
enum class OrdinalShift : std::uint8_t
{
    None = 0,
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

// invariant: the raw direction mass moved; the regression/recovery reading is the consumer's.
enum class OrdinalDriftDirection : std::uint8_t
{
    None = 0,
    Up = 1,
    Down = 2,
};

// invariant: a W1 verdict is the shift bucket plus the direction, never a distance.
struct OrdinalDrift
{
    OrdinalShift shift{OrdinalShift::None};
    OrdinalDriftDirection direction{OrdinalDriftDirection::None};
};

// pre: previous and current are binned on the SAME schedule; the caller gates the schedule-id
// comparability.
// post: the exact 1-D Wasserstein-1 distance reduced to a bucket and a direction.
// post: a zero total on either side is a degenerate pairing and returns {None, None}.
// invariant: exact integers throughout -- a 128-bit signed reducer and a cross-multiply against
// frozen thresholds, so no float and no division reach the verdict.
// refs: SRC-D-W1-1, SRC-D-W1-4, ADR-31.D2
[[nodiscard]] inline OrdinalDrift ordinal_w1(const std::vector<std::uint64_t>& previous,
                                             const std::vector<std::uint64_t>& current,
                                             std::uint64_t previous_total,
                                             std::uint64_t current_total)
{
    using insight::det::i128;
    using insight::det::u128;

    // invariant: frozen octave bands compared by exact integer cross-multiply -- 5 octaves HIGH, 2
    // MED, 0.5 LOW -- and biased conservative, so a real regime shift never reads None.
    // refs: STU-3.A1
    constexpr std::int64_t kHighNum{5}, kHighDen{1};
    constexpr std::int64_t kMedNum{2}, kMedDen{1};
    constexpr std::int64_t kLowNum{1}, kLowDen{2};

    if (previous_total == 0 || current_total == 0)
        // assert: both totals are non-zero below, so the denominator cannot be zero.
        return {};

    const i128 total_a{static_cast<i128>(u128{previous_total})};
    const i128 total_b{static_cast<i128>(u128{current_total})};
    insight::det::FixedReducer numerator_reducer;
    insight::det::FixedReducer direction_reducer;
    std::uint64_t cum_a{0};
    std::uint64_t cum_b{0};
    const std::size_t bins{std::min(previous.size(), current.size())};
    for (std::size_t i{0}; i < bins; ++i)
    {
        cum_a += previous[i];
        cum_b += current[i];
        const i128 a_term{static_cast<i128>(u128{cum_a}) * total_b};
        const i128 b_term{static_cast<i128>(u128{cum_b}) * total_a};
        const i128 signed_term{b_term + (-a_term)};
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

    const i128 direction{direction_reducer.raw()};
    if (!(direction >= i128{0}))
        drift.direction = OrdinalDriftDirection::Up;
    else if (direction >= i128{1})
        drift.direction = OrdinalDriftDirection::Down;

    return drift;
}

// invariant: namespace-scope and exported, because dominant_component_of's signature names the
// component map's full type from the stats partition.
// invariant: a steady-state hit constructs no std::string key; a miss still copies, so key bytes,
// map ordering and determinism are unchanged.
// refs: ADR-9.D2
struct TransparentStringHash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};
// invariant: orders exactly as std::less over (level, component bytes, role), so admitting a
// string_view middle changes which types may ask and never where a key sorts.
struct TransparentCubeKeyLess
{
    using is_transparent = void;
    template <typename LhsString, typename RhsString>
    [[nodiscard]] bool
    operator()(const std::tuple<LogLevel, LhsString, StructuralRole>& lhs,
               const std::tuple<LogLevel, RhsString, StructuralRole>& rhs) const noexcept
    {
        if (std::get<0>(lhs) != std::get<0>(rhs))
            return std::get<0>(lhs) < std::get<0>(rhs);
        const std::string_view lhs_component{std::get<1>(lhs)};
        const std::string_view rhs_component{std::get<1>(rhs)};
        if (const int order{lhs_component.compare(rhs_component)}; order != 0)
            return order < 0;
        return std::get<2>(lhs) < std::get<2>(rhs);
    }
};

// invariant: the single TemplateId -> template_str home, owned outside the document and injected at
// the display seams.
// invariant: append-only and intern-once-per-id, so the first writer wins.
// invariant: node-stable storage, so a returned view stays valid for the registry's lifetime.
// refs: ADR-16.D3, SRC-D-TIR-5
class TemplateRegistry
{
  public:
    TemplateRegistry() = default;
    TemplateRegistry(const TemplateRegistry&) = default;
    TemplateRegistry(TemplateRegistry&&) noexcept = default;
    TemplateRegistry& operator=(const TemplateRegistry&) = default;
    TemplateRegistry& operator=(TemplateRegistry&&) noexcept = default;
    ~TemplateRegistry() = default;

    // post: interns on first sight and returns a view stable for the registry's lifetime.
    std::string_view intern(TemplateId template_id, std::string_view template_str);
    // post: the interned string for template_id, or an empty view when the id is unknown.
    [[nodiscard]] std::string_view lookup(TemplateId template_id) const noexcept;
    [[nodiscard]] bool contains(TemplateId template_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;
    // post: ids already present keep their string; the union is the merged display vocabulary.
    void merge(const TemplateRegistry& other);

  private:
    std::unordered_map<TemplateId, std::string> table_;
};

// invariant: the measurand is the presence of CONTENT oscillating over the observed window range,
// never the set of detectors that fire.
// invariant: Unretained is a boundary that could not be read; Absent is the strictly stronger claim
// and is available only where the window's retained set was exhaustive.
// invariant: EmptyRange is the identity's projection and contributes nothing at a boundary.
// refs: DN-50.D4
enum class PresenceSymbol : std::uint8_t
{
    EmptyRange = 0,
    Unretained = 1,
    Absent = 2,
    Present = 3,
};

// invariant: the product is ASSOCIATIVE, so one element per block folds at every ladder level.
// invariant: NOT commutative -- the boundary term is orientation-sensitive, so operands are applied
// in window order.
// invariant: a default-constructed value IS the identity: span 0 and both projections EmptyRange.
// refs: DN-50.D4
struct PresenceChurn
{
    // invariant: base windows this element covers; 0 is the identity.
    std::uint32_t span_windows{0};
    // invariant: readable boundaries where presence changed.
    std::uint32_t transitions{0};
    // invariant: boundaries where retention, not presence, changed.
    std::uint32_t indeterminate{0};
    // invariant: presence in the FIRST window of the range.
    PresenceSymbol first{PresenceSymbol::EmptyRange};
    // invariant: presence in the LAST window of the range.
    PresenceSymbol last{PresenceSymbol::EmptyRange};

    [[nodiscard]] bool operator==(const PresenceChurn&) const noexcept = default;
};

// post: the base element for a template the producer retained in one observed window.
[[nodiscard]] constexpr PresenceChurn presence_churn_of_retained_window() noexcept
{
    return {.span_windows = 1,
            .transitions = 0,
            .indeterminate = 0,
            .first = PresenceSymbol::Present,
            .last = PresenceSymbol::Present};
}

// pre: retention_exhaustive answers whether the range retained every template it observed.
// post: an exhaustive range contributes a definite Absent; a truncated one declares each of its
// span_windows-1 internal boundaries indeterminate rather than counting no transition.
[[nodiscard]] constexpr PresenceChurn
presence_churn_of_unretained_range(std::uint32_t span_windows, bool retention_exhaustive) noexcept
{
    if (span_windows == 0)
        return {};
    if (retention_exhaustive)
        return {.span_windows = span_windows,
                .transitions = 0,
                .indeterminate = 0,
                .first = PresenceSymbol::Absent,
                .last = PresenceSymbol::Absent};
    return {.span_windows = span_windows,
            .transitions = 0,
            .indeterminate = span_windows - 1,
            .first = PresenceSymbol::Unretained,
            .last = PresenceSymbol::Unretained};
}

// pre: earlier and later are the two operands IN WINDOW ORDER; compose() establishes that from the
// documents' window envelopes rather than trusting a caller.
[[nodiscard]] constexpr PresenceChurn compose_presence_churn(const PresenceChurn& earlier,
                                                             const PresenceChurn& later) noexcept
{
    // assert: an empty range contributes no boundary term, where an unretained one contributes an
    // indeterminate.
    if (earlier.span_windows == 0)
        return later;
    if (later.span_windows == 0)
        return earlier;
    const bool boundary_readable{earlier.last != PresenceSymbol::Unretained &&
                                 later.first != PresenceSymbol::Unretained};
    return {.span_windows = earlier.span_windows + later.span_windows,
            .transitions = earlier.transitions + later.transitions +
                           (boundary_readable && earlier.last != later.first ? 1U : 0U),
            .indeterminate =
                earlier.indeterminate + later.indeterminate + (boundary_readable ? 0U : 1U),
            .first = earlier.first,
            .last = later.last};
}

// invariant: the document-root roll-up of per-template churn over the declared horizon.
// refs: DN-50.D4, DN-50.D5
struct PresenceChurnSummary
{
    // invariant: the presence predicate is DECLARED here so indeterminate means the same thing
    // across producers; the serializer writes this value and never a literal of its own.
    static constexpr std::string_view kHorizon{"top_k_union_reservoir"};

    // invariant: a one-window range cannot carry a transition, so below this span the block is
    // omitted from the wire rather than emitted as a tautology.
    static constexpr std::uint32_t kMinimumInformativeSpan{2};

    std::uint32_t span_windows{0};
    std::uint32_t templates_with_churn{0};
    std::uint64_t total_transitions{0};
    std::uint64_t total_indeterminate{0};

    [[nodiscard]] bool operator==(const PresenceChurnSummary&) const noexcept = default;
};

struct TopKEntry
{
    TemplateId
        // invariant: a content-hash POD, rendered to a prefixed hex string at the serialize seam.
        // refs: SRC-D-TIR-5
        template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    // invariant: the level AND its provenance; DECLARED means at least one observation carried the
    // level in a position whose meaning is the level.
    // invariant: the provenance half is domain-only and never serialised.
    // refs: DN-32.D3
    std::optional<EventLevel> dominant_level;
    // invariant: the template's dominant canon component -- the per-template WHERE label, populated
    // independently of the cube.
    // invariant: disengaged when the format carried no component, never an empty string.
    // refs: SRC-D-WHERE-2, SRC-D-WHERE-6
    std::optional<std::string> dominant_component;
    // invariant: empty unless MetaLogConfig::max_param_histograms > 0.
    std::vector<FieldHistogram> field_histograms;
    // invariant: one per declared ordinal field seen on this template; empty unless
    // MetaLogConfig::max_param_histograms > 0.
    // refs: SRC-D-W1-2, SRC-D-W1-5
    std::vector<OrdinalHistogram> ordinal_histograms;
    // invariant: the identity on a document carrying no churn observation, and the monoid product
    // on a composed one.
    // refs: DN-50.D4
    PresenceChurn presence_churn{};
};

// invariant: raw integer structural facts about the dimensions a window carries, never a baked
// verdict -- each consumer applies its own predicate over them.
// invariant: a pure function of the frozen ordered window, so the counts are order-independent and
// bit-identical across standard libraries.
// invariant: present on every document close_window() produces; compose() sets none.
// refs: SRC-D-WHERE-4, SRC-D-WHERE-5
struct AcquisitionBlock
{
    // invariant: events that carried a non-empty canon component, and the distinct values seen.
    // invariant: lines_observed is WindowBlock's and is deliberately not duplicated here.
    std::uint64_t records_with_component{0};
    std::uint64_t distinct_components{0};

    // invariant: the observed distinct-value COUNT per cube dimension, never the count-per-value
    // distribution.
    // invariant: published per dimension so a consumer can tell WHICH dimension is exploding.
    std::uint64_t level_cardinality{0};
    std::uint64_t role_cardinality{0};

    // invariant: entry d is the distinct count of WHERE prefixes truncated to depth d+1, coarsest
    // first.
    // invariant: one element today, because the WHERE axis is a single depth-1 chain.
    std::vector<std::uint64_t> where_cardinality_per_depth;

    // invariant: the closed cube's condensed cell count, read off the cube built before this block.
    // invariant: the combinatorial bound is derivable from the per-dimension factors and is
    // therefore not stored.
    std::uint64_t closed_cells{0};

    // invariant: raw threshold-free counts; span_records > 0 is what licenses trace vocabulary.
    // invariant: orphan_parent_edges counts spans whose declared parent did not resolve in the
    // window -- counted, never guessed.
    // invariant: both 0 for a non-span window.
    // refs: SRC-D-OTEL-13, SRC-D-OTEL-11
    std::uint64_t span_records{0};
    std::uint64_t orphan_parent_edges{0};
    // invariant: declared cross-trace LINK targets that did not resolve in this window -- counted,
    // never guessed, and sibling to orphan_parent_edges.
    // invariant: 0 for a window with no unresolved links.
    // refs: SRC-D-OTEL-9, SRC-D-OTEL-11
    std::uint64_t orphan_link_edges{0};

    [[nodiscard]] bool operator==(const AcquisitionBlock&) const noexcept = default;
};

// invariant: the observed span tree projected to (caller_service -> callee_service) at component
// granularity, derived at close time.
// invariant: its own additive flag-gated block with its own diff -- not a cube dimension and not
// folded into top_ngrams.
// invariant: integer weights in sorted canonical (caller, callee) byte order, no float.
// refs: ADR-29.D2, SRC-D-OTEL-21
struct ServiceEdge
{
    // invariant: the PARENT span's service.name.
    std::string caller;
    // invariant: the CHILD span's service.name.
    std::string callee;
    // invariant: observed parent-to-child edges at this component pair this window.
    std::uint64_t weight{0};
    [[nodiscard]] bool operator==(const ServiceEdge&) const noexcept = default;
};

// invariant: present iff the window had trace substrate; a non-span window OMITS the block, and
// that absence reads unknown rather than no edges.
// invariant: self-edges are excluded at derivation.
// invariant: edges are sorted by (caller, callee) and bounded to max_service_edges by weight with a
// canonical-key tie-break; dropped_edges counts those beyond the cap.
// refs: SRC-D-OTEL-21, SRC-D-OTEL-20
struct ServiceEdgeBlock
{
    std::vector<ServiceEdge> edges;
    std::uint64_t dropped_edges{0};
    [[nodiscard]] bool operator==(const ServiceEdgeBlock&) const noexcept = default;
};

// invariant: the identity of the semantic ruleset that SEGMENTED this document -- the comparability
// key.
// invariant: an additive flag-gated block, so absence means a legacy producer and no wire version
// moves.
// invariant: two documents are comparable iff their semantic_identity matches; on mismatch the
// consumer re-segments where raw inputs exist and refuses otherwise, never compares.
// refs: SRC-II-7, ADR-17.D3
struct RulesetPackageRef
{
    std::string name;
    std::string version;
    [[nodiscard]] bool operator==(const RulesetPackageRef&) const noexcept = default;
};

struct RulesetIdentity
{
    // invariant: the composed content hash over the actual composed rows, and the KEY.
    std::string semantic_identity;
    // invariant: the composed package set, in canonical package-sorted order, for legibility only
    // -- the hash is the key.
    std::vector<RulesetPackageRef> packages;
    [[nodiscard]] bool operator==(const RulesetIdentity&) const noexcept = default;
};

// invariant: which delivery layers the declarer said wrap this run's lines. DISCLOSURE, never
// evidence: a wrong declaration stays wrong and the declarer owns it.
// invariant: not identity and not a comparability gate -- the transform GRAMMAR enters
// semantic_identity and the per-run declaration does not.
// invariant: nothing here licenses a comparability statement ACROSS transport.
// refs: ADR-23.D2, ADR-23.D4, ADR-23.D6
struct TransportDeclaration
{
    // invariant: ordered outside-in, the order the delivery layers were applied.
    // invariant: empty means nothing was declared, which is a fact about the run and not the same
    // as the member being absent.
    std::vector<std::string> names;
    // invariant: the catalogue the names resolve against; a name recorded without its catalogue
    // version is unresolvable by a later reader.
    // refs: ADR-23.D3
    std::string catalog_version{insight::transport::kTransportCatalogVersion};
    [[nodiscard]] bool operator==(const TransportDeclaration&) const noexcept = default;
};

// invariant: kind categorical is a flat low-card category; kind chain is a single-parent roll-up
// hierarchy carrying its ordered levels coarsest first plus a frozen floor_depth.
struct CubeAxis
{
    // invariant: one of level, structural_role or where.
    std::string name;
    // invariant: either categorical or chain.
    std::string kind;
    // invariant: chain axes only -- the ordered chain levels, coarsest first.
    std::optional<std::vector<std::string>> chain;
    // invariant: chain axes only -- the retained depth; below the full depth the axis is
    // prefix-truncated, and 0 means the axis was dropped.
    std::optional<std::uint32_t> floor_depth;
    // invariant: for an ORDINAL axis, how many lowest-severity levels the window merged into one
    // band from the bottom, NEVER across the ERROR/FATAL frontier.
    // invariant: absent or 0 means no banding; two cubes compare only at equal floor_depth AND
    // band_floor, so the axes carry the collapse and a mismatch is detectable.
    std::optional<std::uint32_t> band_floor;
    [[nodiscard]] bool operator==(const CubeAxis&) const noexcept = default;
};

// invariant: an ABSENT axis key means aggregated; the wire object is open over axis names.
// invariant: a categorical value is a string and a chain value is an ordered prefix path.
struct CubeCoord
{
    // invariant: categorical -- the severity.
    std::optional<std::string> level;
    // invariant: chain -- the WHERE prefix path, entry i being chain level i.
    std::optional<std::vector<std::string>> where;
    // invariant: categorical -- the kind-framing marker.
    std::optional<std::string> structural_role;
    // invariant: the signed, polarity-MUTE latency shift band a component crossed into, oriented
    // previous to current; metalog does not judge good or bad.
    // invariant: present ONLY on a cube_diff emerging-border cell whose component shifted, and
    // always absent on a stored cube cell.
    std::optional<std::string> latency_shift;
    [[nodiscard]] bool operator==(const CubeCoord&) const noexcept = default;
};

// invariant: one closed cell -- its coordinate plus the distributive integer COUNT measure.
struct CubeCell
{
    CubeCoord coord;
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const CubeCell&) const noexcept = default;
};

// invariant: the aggregated-WHERE sentinel for a base row's component_id, numerically identical to
// the cube's internal star so a retained and a re-interned base agree.
inline constexpr std::uint32_t kStarComponent{std::numeric_limits<std::uint32_t>::max()};

// invariant: one retained interned base joint, domain-only and never serialised.
// invariant: component_id indexes CubeBlock::base_component_dict, and level is already
// cube-normalised.
struct CubeBaseRow
{
    LogLevel level{LogLevel::Info};
    std::uint32_t component_id{kStarComponent};
    StructuralRole role{StructuralRole::None};
    std::uint64_t count{0};
    [[nodiscard]] bool operator==(const CubeBaseRow&) const noexcept = default;
};

// invariant: an intra-window CLOSED cube over low-card categorical axes; an attributor and
// projector, never a detector and never the source of truth for a 1-D marginal.
// invariant: always built for a raw window and permanently inside the canonicalization_version
// comparability contract.
// invariant: cells is the condensed closed representation in canonical coord-sorted order,and the
// closure regenerates every non-closed cell losslessly.
// invariant: cell_count over raw_cell_count is the condensation the closure achieved.
// refs: F-SRC-metalog-spec:SPEC.md
struct CubeBlock
{
    std::vector<CubeAxis> axes;
    std::vector<CubeCell> cells;
    std::uint64_t cell_count{0};
    std::uint64_t raw_cell_count{0};
    // invariant: the STATIC producer cell budget the per-window collapse bounds cells by.
    // invariant: it dominates every other term of the envelope formula, so an undeclared budget
    // leaves a consumer unable to price the block.
    // invariant: disengaged rather than zero on a hand-built cube -- the schema's minimum is 1.
    std::optional<std::uint64_t> cell_budget;

    // invariant: domain-only and never serialised, so the wire format is unchanged.
    // invariant: the interned base plus its sorted component dictionary, immutable after
    // construction; empty on a cube that did not retain it, and compose/diff then recover it.
    // invariant: a pure cache of the closed cells, so it is excluded from equality.
    std::vector<CubeBaseRow> base;
    std::vector<std::string> base_component_dict;

    // invariant: identity is the wire-relevant state only; the retained base is a derived cache.
    [[nodiscard]] bool operator==(const CubeBlock& other) const noexcept
    {
        return axes == other.axes && cells == other.cells && cell_count == other.cell_count &&
               raw_cell_count == other.raw_cell_count && cell_budget == other.cell_budget;
    }
};

// invariant: the monitor's per-axis index, distinct from the axis descriptor CubeAxis.
enum class CardinalityAxis : std::uint8_t
{
    Level = 0,
    Component = 1,
    Role = 2
};

// invariant: a pure function of the closed cube, for observability only -- it never feeds the
// deterministic content stream.
// invariant: kCellsHard is the cube.cpp collapse budget, asserted by tests.
struct CubeCardinalityStat
{
    static constexpr std::size_t kAxisCount{3};
    static constexpr std::uint64_t kCellsHard{
        // invariant: the collapse budget; the cube never exceeds it.
        4096};

    std::uint64_t cells{0};
    // invariant: distinct values per axis, indexed by CardinalityAxis.
    std::array<std::uint32_t, kAxisCount> per_axis{};
};

// invariant: a level is in failure iff it is Error or Fatal. Unknown sorts numerically ABOVE Fatal
// and is NOT a failure, so this is a membership test and never a >= Error compare.
// invariant: exported rather than TU-local, so the two halves of one report cannot decide failure
// differently.
// refs: DN-64.D3
[[nodiscard]] constexpr bool is_failure_level(LogLevel level) noexcept
{
    return level == LogLevel::Error || level == LogLevel::Fatal;
}
[[nodiscard]] constexpr bool is_failure_level(const std::optional<EventLevel>& level) noexcept
{
    return level.has_value() && is_failure_level(level->value());
}

// invariant: the full scale of ReservoirEntry::salience -- the PRODUCER's bound, exported so no
// consumer spells its own literal to divide by.
// invariant: the product of the two ladders salience_score multiplies, static_asserted in
// salience.cpp against those rungs.
// refs: DN-64.D3
inline constexpr std::uint32_t kSalienceFullScale{10000U};

// invariant: WHICH axis retained a template -- the argmax of the soft max salience_score takes over
// its five peer axes, stamped where the max is taken.
// invariant: the two published ordinals are two of three severity inputs, so a consumer re-deriving
// the argmax from them cannot name the level, terminator or failure-cue arms.
// refs: DN-64.D3
enum class RetentionAxis : std::uint8_t
{
    // invariant: the dominant_level's severity band.
    Level,
    // invariant: the LEVEL-BLIND token-lexicon tier.
    // refs: SRC-D-PROV-1
    FailureCue,
    // invariant: the declared structural failure marker.
    Terminator,
    // invariant: the STRUCTURE axis -- reached only via a rare incoming transition.
    StructuralSurprise,
    // invariant: the TIME axis -- first seen late within the window.
    Novelty
};

// invariant: the parameter is the plain enum, so a caller holding the optional owns the word
// absence reads as.
// invariant: the tail names the not-an-axis state rather than guessing, and stays distinct from any
// absence word.
// refs: DN-64.D6
[[nodiscard]] inline std::string_view to_string(RetentionAxis axis) noexcept
{
    switch (axis)
    {
    case RetentionAxis::Level:
        return "level";
    case RetentionAxis::FailureCue:
        return "failure_cue";
    case RetentionAxis::Terminator:
        return "terminator";
    case RetentionAxis::StructuralSurprise:
        return "structural_surprise";
    case RetentionAxis::Novelty:
        return "novelty";
    }
    return "<not-an-axis>";
}

// invariant: a template retained by intrinsic SALIENCE rather than frequency, disjoint from top_k
// and excluded from the tail residual.
// invariant: self-describing -- it carries the salience and its inputs, so a consumer can attribute
// the retention.
// refs: ADR-31.D8
struct ReservoirEntry
{
    // invariant: the same content-hash POD as TopKEntry's.
    // refs: SRC-D-TIR-5
    TemplateId template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    // invariant: the level AND its provenance; DECLARED means at least one observation carried the
    // level in a position whose meaning is the level.
    // invariant: the provenance half is domain-only and never serialised.
    // refs: DN-32.D3
    std::optional<EventLevel> dominant_level;
    // invariant: the template's dominant canon component -- the WHERE label, populated
    // independently of the cube and disengaged when the format carried no component.
    // invariant: distinct from cube_coord, which is the cube's LOCATION cross.
    // refs: SRC-D-WHERE-2
    std::optional<std::string> dominant_component;
    StructuralRole structural_role{StructuralRole::None};
    // invariant: a 0..100 band derived from the lowest-probability incoming transition in the
    // behaviour graph; above 0 the template was reached only off the dominant path.
    std::uint32_t structural_surprise{0};
    // invariant: a 0..100 band over the first-seen position within this document's own span; above
    // 0 the template emerged during the window rather than being present from the start.
    // invariant: self-relative and re-derivable on compose from merged provenance -- never absence
    // from a baseline.
    std::uint32_t novelty{0};
    // invariant: the quantized integer salience, higher meaning more salient.
    std::uint32_t salience{0};
    // invariant: the retention argmax stamped where the max is taken, never re-derived downstream.
    // invariant: domain-only and never serialised.
    // invariant: disengaged is a real third state -- an entry no salience computation produced has
    // no argmax, and the honest reading is that this entry does not say.
    // invariant: every entry this package emits engages it, because both filling sites admit a
    // candidate only under a positive salience score, which is when the verdict is engaged.
    // refs: DN-64.D3, DN-64.D6
    std::optional<RetentionAxis> retention_axis;
    // invariant: the reconciled first-seen ordinal of this template within the window, bounded by
    // the reservoir size; populated only when a re-derivation coordinate is configured.
    // invariant: never a per-line coordinate.
    std::optional<std::uint64_t> within_window_ordinal;
    // invariant: the LOCATION cross into the cube -- level plus WHERE path only, read-only and
    // one-way, so it carries no salience back and never re-ranks a cell or the border.
    // invariant: a pure function of the entry's dominant level and component; structural_role is
    // deliberately left unset.
    std::optional<CubeCoord> cube_coord;
    // invariant: presence churn exactly as on TopKEntry -- the declared horizon is top_k UNION
    // reservoir, so a reservoir row folds like any other retained row.
    // invariant: domain-only, because the specification grants the extensions container at
    // stats.top_k[] and at the document root and NOT at stats.reservoir[].
    // refs: DN-50.D4, F-SRC-metalog-spec:SPEC.md
    PresenceChurn presence_churn{};
};

struct BranchingEntry
{
    TemplateId template_id;
    std::uint64_t fanout{0};
    std::uint64_t total_outgoing{0};
    double entropy_bits{0.0};
};

// invariant: the bounded shape of the long tail -- how concentrated and how loud it is, without
// expanding top_k.
// invariant: tail_entropy_bits is the Shannon entropy over the row-normalised tail, and collapses
// toward 0 when one template dominates it.
// invariant: tail_max_rate is the loudest tail template's share of lines_observed.
// invariant: all three fields are REQUIRED when the block is present -- a producer emits all three
// or omits the block.
// refs: F-SRC-metalog-spec:SPEC.md
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
    // invariant: Shannon entropy over the full template distribution.
    std::optional<double> entropy_bits;
    // invariant: present when the tail holds at least one template, absent otherwise.
    std::optional<TailSummary> tail_summary;
    // invariant: salient templates retained below top_k; empty unless MetaLogConfig::reservoir_size
    // > 0.
    // invariant: the tail residual excludes these, so a promoted template is not double-counted.
    std::vector<ReservoirEntry> reservoir;
    // invariant: the cap this document's reservoir was admitted under; ABSENT is a declared
    // posture, because the specification reads an omitted cap as no claim rather than a bound.
    // invariant: a declaring producer owes the clause -- the array MUST be bounded by the value.
    // invariant: compose() sets it to the minimum over the caps its inputs actually declared, and
    // omits it only when both inputs declared none.
    // refs: DN-56.D2
    std::optional<std::size_t> reservoir_size;
};

// post: whether absence from this document's retained set means absence from every window it covers
// -- the predicate deciding Absent against Unretained.
// invariant: derived from two members the STANDARD already carries, so a consumer re-folding two
// documents off the wire computes the same answer the producer did.
// invariant: sound on a composed document, because compose() adds both inputs' tail_count to the
// merged visible tail mass; reading tail_unique alone would NOT be sound there.
// refs: DN-50.D4
[[nodiscard]] inline bool retention_is_exhaustive(const StatsBlock& stats) noexcept
{
    return stats.tail_count == 0 && stats.tail_unique == 0;
}

// invariant: sequence holds the content-derived template ids in observed order, sized ngram_size.
struct NGramEntry
{
    std::vector<TemplateId> sequence;
    std::uint64_t count{0};
    // invariant: p(last | prefix).
    double probability{0.0};
};

struct BehaviorBlock
{
    std::size_t ngram_size{2};
    std::vector<NGramEntry> top_ngrams;
    std::size_t top_ngrams_size{0};
    // invariant: n-gram OBSERVATIONS refused at MetaLogConfig::max_ngram_keys BEFORE being counted,
    // never distinct keys -- the distinct count is not knowable.
    // invariant: OPTIONAL because the absence is normative: in a document declaring 0.7.0 or later
    // an omitted key MEANS zero, and in an earlier one it means UNKNOWN.
    // invariant: a producer whose cap never binds therefore emits bytes identical to one with no
    // cap at all.
    // invariant: engaged implies greater than zero, held by whoever sets it and never by the
    // serializer, which passes it through so a broken producer reds a test.
    // refs: ADR-9.D3
    std::optional<std::uint64_t> dropped_ngram_observations;
    std::optional<std::uint64_t> graph_edge_count;
    // invariant: absent when not computed.
    std::optional<std::vector<TemplateId>> dominant_path;
    // invariant: absent when not computed.
    std::optional<std::vector<BranchingEntry>> branching;
    // invariant: the cap branching was truncated to; the specification reads an omitted field as
    // the producer declaring NO cap, and this producer does cap.
    // invariant: present on every document carrying branching, and absent exactly where branching
    // is.
    std::optional<std::size_t> branching_size;
};

struct StabilityBlock
{
    // invariant: RFC 3339 UTC.
    std::string previous_window_end_iso;
    // invariant: KL(current || previous).
    double kl_divergence{0.0};
    // invariant: symmetric Jensen-Shannon in [0, 1], log base 2.
    double js_divergence{0.0};
    std::uint64_t new_templates{0};
    std::uint64_t vanished_templates{0};
    // invariant: producer-defined; this producer uses 1 - js_divergence.
    double stability_score{1.0};
};

struct WindowBlock
{
    // invariant: RFC 3339 UTC.
    std::string start_iso;
    std::string end_iso;
    std::uint64_t duration_seconds{0};
    std::uint64_t lines_observed{0};
};

// invariant: overrides the REPORTED window bounds at close_window, decoupled from the open/close
// machinery times.
// invariant: a deterministic-batch caller supplies the input's parseable-timestamp envelope so the
// window reflects event time; live callers omit it and the bounds are the open/close times.
// refs: BIB:determinism_model
struct ReportedWindowBounds
{
    Timestamp start;
    Timestamp end;
};

// invariant: this package's own version, stamped into producer.version. ONE spelling for the
// package -- the DTO default and the engine config both read it.
// invariant: hand-carried rather than fed from the recipe: this interface unit is recompiled by
// every consumer.
// invariant: a compile definition here would let a consumer's build of the api module disagree with
// the linked engine.
// refs: OPS-1.S15
inline constexpr std::string_view kProducerVersion{"1.10.4"};

// invariant: the specification edition this producer writes against, stamped into metalog_version.
// ONE spelling, and a DIFFERENT axis from kProducerVersion.
// invariant: only the specification's MAJOR is normatively coupled, and the version is owned by
// metalog-spec rather than by this workspace's release baseline.
// invariant: hand-carried for kProducerVersion's reason, which applies unchanged.
// refs: F-SRC-metalog-spec:SPEC.md
inline constexpr std::string_view kMetaLogSpecVersion{"0.10.0"};

struct ProducerBlock
{
    std::string name{"insight"};
    std::string version{kProducerVersion};
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

// invariant: makes a window addressable back to its source, so raw(window) equals replay(source,
// bounds) with no raw buffering.
// invariant: DESCRIPTIVE metadata only -- bit-identical across replays and it MUST NOT feed any
// deterministic-content, retention or salience computation.
// invariant: present only when a source_ref is configured.
// refs: F-SRC-metalog-spec:SPEC.md
struct SourceRef
{
    // invariant: selects the resolver; opaque to the specification, so a producer MUST NOT assume a
    // particular one.
    std::string resolver_kind;
    // invariant: an opaque resolvable handle whose meaning the environment defines.
    std::string handle;
    [[nodiscard]] bool operator==(const SourceRef&) const noexcept = default;
};

struct EventTimeBounds
{
    // invariant: the window is [start_tick, end_tick) in EVENT-TIME integer ticks, bit-identical
    // across replays.
    // invariant: window membership MUST be by event time only, never the global sequence counter or
    // replay depth.
    std::uint64_t start_tick{0};
    std::uint64_t end_tick{0};
    [[nodiscard]] bool operator==(const EventTimeBounds&) const noexcept = default;
};

struct ReDerivationCoordinate
{
    // invariant: a coordinate is XOR -- either RAW with source_ref and bounds set and children
    // absent, or COMPOSED with children set and both of those absent.
    // invariant: sentinel values on a composed coordinate are forbidden, so consumers discriminate
    // on the presence of children.
    std::optional<SourceRef> source_ref;
    std::optional<EventTimeBounds> bounds;
    // invariant: reproduction aids, admissible on EITHER kind, because canon output depends on
    // canon code and config and not on raw bytes alone.
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> config_hash;
    // invariant: composed documents only -- the non-empty SET of children's coordinates.
    // invariant: a composed coordinate resolves through its children, never a coarse first-last
    // bound, which would over-claim across gaps, shards and sources.
    std::optional<std::vector<ReDerivationCoordinate>> children;
    [[nodiscard]] bool operator==(const ReDerivationCoordinate&) const noexcept = default;
};

// invariant: one input that fed a composed document.
// refs: F-SRC-metalog-spec:SPEC.md
struct ProvenanceEntry
{
    std::string window_start_iso;
    std::string window_end_iso;
    SourceBlock source;
    std::uint64_t lines_observed{0};
    std::optional<std::string> document_id;
    // invariant: the input's own coordinate, so a composed document's coordinate resolves to this
    // raw child; absent when the input had none.
    std::optional<ReDerivationCoordinate> coordinate;
};

// invariant: this producer emits the specification's INLINE template-string mode only -- the dedup
// and id-only arms were never wired.
// refs: SRC-D-TIR-5, ADR-9.D2
struct MetaLogDocument
{
    // invariant: the specification edition the bytes were written against; only its MAJOR is
    // normatively coupled, and the producer's own version is a separate axis.
    std::string metalog_version{kMetaLogSpecVersion};
    ProducerBlock producer{};
    WindowBlock window{};
    SourceBlock source{};
    StatsBlock stats{};
    std::optional<BehaviorBlock> behavior;
    std::optional<StabilityBlock> stability;
    // invariant: absent unless the document is composed.
    std::optional<std::vector<ProvenanceEntry>> provenance;
    // invariant: opaque names of the contract the document was produced under, set from
    // MetaLogConfig at close_window; they gate compose() and diff() comparability.
    std::optional<std::string> canonicalization_version;
    std::optional<std::string> retention_profile;
    // invariant: present whenever the producer was configured with a source_ref; a composed
    // document carries children instead of addressing a single source.
    std::optional<ReDerivationCoordinate> coordinate;
    // invariant: always built for a raw window and bounded by the per-window collapse.
    // invariant: has_cube is the representation's presence flag; a COMPOSED document clears it when
    // EITHER input omitted a cube, which is the only way it is ever cleared.
    // invariant: the axis SET is frozen per canonicalization_version, but the per-window collapse
    // stamps are not, so compose and diff read the pair at its minimal common collapse.
    // refs: DN-42.D17
    // invariant: an explicit presence flag plus an inline value, NOT std::optional<CubeBlock>: MSVC
    // miscompiles the synthesized optional copy in consumer module translation units.
    bool has_cube{false};
    CubeBlock cube{};
    // invariant: the window's structural facts seeding the WHERE disposition and the cube dimension
    // self-assessment.
    // invariant: set by close_window() on every raw window; compose() sets none.
    // invariant: stamped once at close and only read, so std::optional is sound despite the owned
    // per-depth vector.
    // refs: SRC-D-WHERE-4
    std::optional<AcquisitionBlock> acquisition;
    // invariant: present iff the window had trace substrate; absent for a non-span window, and that
    // absence reads unknown.
    // invariant: stamped once at close and only read, so std::optional is sound despite the owned
    // vector.
    // refs: SRC-D-OTEL-21
    std::optional<ServiceEdgeBlock> service_edges;
    // invariant: the semantic_identity and package list of the ruleset that segmented this
    // document; absent means a legacy producer.
    // invariant: stamped once at close and only read, so std::optional is sound.
    // refs: SRC-II-7, ADR-17.D3
    std::optional<RulesetIdentity> ruleset;
    // invariant: the producer stamps it on EVERY closed window -- an empty names[] says nothing was
    // declared, and that must never degrade into the member's absence.
    // invariant: engaged only when both inputs of a compose agree, because a composed document
    // whose inputs declared different stacks makes no single declaration.
    // refs: ADR-23.D4
    std::optional<TransportDeclaration> transport;
    // invariant: one four-class scalar per run, stamped by the producing orchestration on a
    // WHOLE-RUN document; Unknown is both the default and the wire absence.
    // invariant: not a cube dimension -- the outcome labels the whole run, and a per-quantum slice
    // document correctly keeps Unknown.
    // refs: SRC-D-OUT-RUN-1, ADR-17.D5
    insight::RunOutcome run_outcome{insight::RunOutcome::Unknown};
    // invariant: the presence-churn roll-up over the declared horizon; ABSENT on a document that
    // carries no churn observation at all, which is the monoid identity.
    // refs: DN-50.D4, DN-50.D5
    std::optional<PresenceChurnSummary> presence_churn;
};

// invariant: the cube, the per-template dominant_component WHERE leaf and the per-window
// acquisition block are ALWAYS emitted -- there is no opt-in gate for any of them.
// refs: SRC-D-WHERE-2
struct MetaLogConfig
{
    static constexpr std::size_t kDefaultTopKSize = 64;
    static constexpr std::size_t kDefaultTopNgramsSize = 32;
    static constexpr std::size_t kDefaultMaxNgramKeys = 4096;
    static constexpr std::size_t kDefaultTopBranchingSize = 64;
    static constexpr std::size_t kDefaultDominantPathMaxSteps = 8;
    static constexpr std::size_t kDefaultMaxActiveTraces = 4096;
    // invariant: the span_id to template bound.
    // refs: SRC-D-OTEL-11
    static constexpr std::size_t kDefaultMaxActiveSpans = 16384;
    // invariant: the service_edges emit cap.
    // refs: SRC-D-OTEL-21
    static constexpr std::size_t kDefaultMaxServiceEdges = 4096;

    // invariant: max entries kept in stats.top_k; the rest are summarised into tail_count and
    // tail_unique. 0 skips top_k emission and the document stays bounded.
    std::size_t top_k_size{kDefaultTopKSize};

    // invariant: max templates retained by salience below top_k; 0 disables the reservoir and
    // leaves pure frequency retention.
    std::size_t reservoir_size{0};

    // invariant: max exemplars admitted per kind (structural_role by dominant_level), so the
    // reservoir optimises coverage of distinct salient kinds over depth; 0 means no cap.
    std::size_t reservoir_per_kind_cap{0};

    // invariant: a bounded floor of the reservoir slots held exclusively for error-class templates,
    // so non-failure salience can never evict a real failure.
    // invariant: admitted by salience then template_id, and EXEMPT from the per-kind cap.
    // invariant: clamped to reservoir_size; 0 disables the reserve.
    // invariant: a retention policy and not template identity, so it moves no
    // canonicalization_version and no wire version.
    // refs: SRC-D-RNK-2
    std::size_t reservoir_error_reserve{0};

    // invariant: the single n-gram order emitted in the behaviour block; 2 or 3.
    std::size_t ngram_size{2};

    // invariant: max entries kept in behavior.top_ngrams; 0 disables the behaviour block.
    std::size_t top_ngrams_size{kDefaultTopNgramsSize};

    // invariant: max distinct n-gram keys retained; past the cap, counts on existing keys keep
    // updating and new keys are dropped.
    std::size_t max_ngram_keys{kDefaultMaxNgramKeys};

    // invariant: default true -- an OTEL event forms its n-gram WITHIN its trace.
    // invariant: false is the CONTROL ARM, reproducing the polluted global-order graph on the same
    // input; non-OTEL ingest is byte-identical either way.
    // refs: ADR-29.D1
    bool trace_scoping_enabled{true};

    // invariant: max concurrent OTEL traces whose n-gram ring is held; a ring is the last one or
    // two template ids, never a per-trace sub-fingerprint.
    // invariant: overflow evicts the oldest-inserted trace's ring (deterministic FIFO), losing at
    // most one cross-record edge for that trace and never its membership.
    // invariant: consulted ONLY for events carrying a trace id.
    // refs: SRC-D-OTEL-1
    std::size_t max_active_traces{kDefaultMaxActiveTraces};

    // invariant: max span_id to template entries held in a window for close-time parent-edge
    // resolution.
    // invariant: a parent evicted past this bound or outside the window yields no edge and one
    // orphan_parent_edges fact -- counted, never guessed.
    // invariant: deterministic FIFO eviction of the oldest-inserted span; consulted ONLY for
    // records carrying is_span, and 0 disables the bound.
    // refs: SRC-D-OTEL-11
    std::size_t max_active_spans{kDefaultMaxActiveSpans};

    // invariant: max service_edges emitted; edges beyond it fold into dropped_edges by top-weight-K
    // with a canonical-key tie-break.
    // invariant: the accumulator itself is bounded by topology squared; this is the wire cap.
    // refs: SRC-D-OTEL-21
    std::size_t max_service_edges{kDefaultMaxServiceEdges};

    // invariant: when true, the engine remembers the previous closed window's template frequencies
    // and emits a stability block on every subsequent window.
    bool emit_stability{true};

    // invariant: cap on behavior.branching entries; 0 disables.
    std::size_t top_branching_size{kDefaultTopBranchingSize};

    // invariant: cap on behavior.dominant_path length; 0 disables.
    std::size_t dominant_path_max_steps{kDefaultDominantPathMaxSteps};

    // invariant: reported as producer.version; kProducerVersion owns the value.
    std::string producer_version{kProducerVersion};

    // invariant: when set, close_window stamps a coordinate of this source plus the window's
    // event-time bounds; the engine derives no source of its own.
    // invariant: unset means no coordinate is emitted, which is the conservative default.
    std::optional<SourceRef> source_ref;

    // invariant: opaque strings naming the CONTRACT the document was produced under; they gate
    // compose() and diff() comparability.
    // invariant: retention_profile names the retention parameters in effect and MUST be bumped when
    // any of them change.
    // invariant: canonicalization_version defaults to the canon-owned constant, so a producer
    // cannot silently leave old and new documents falsely comparable.
    // refs: SRC-D-TID-16
    std::optional<std::string> canonicalization_version{
        std::string{insight::kCanonicalizationVersion}};
    std::optional<std::string> retention_profile;
    // invariant: injected by the producing binary; DEFAULT unset, so a producer that does not
    // inject it emits no ruleset block and reads as a legacy producer.
    // invariant: unlike canonicalization_version there is no canon-owned default -- canon ships no
    // default composition, so only the binary declaring its package set knows the hash.
    // refs: SRC-II-7, ADR-17.D2
    std::optional<RulesetIdentity> ruleset;

    // invariant: the stream's DECLARED transport stack, injected by the producing binary and
    // stamped on every closed document.
    // invariant: NOT optional, because the empty names[] IS the degenerate stack.
    // invariant: the producer records the declaration and never resolves, verifies or applies it --
    // peeling is the caller's, and this field is on the recording path only.
    // refs: ADR-23.D4
    TransportDeclaration transport;

    // invariant: max wildcard positions histogrammed per top_k entry; 0 disables and costs one
    // predicted-not-taken branch per ingest call.
    // invariant: above 0 the first min(N, params.size()) wildcard positions are tracked per
    // template bucket, and memory is bounded by top_k_size times N times max_histogram_values.
    std::size_t max_param_histograms{0};

    // invariant: max distinct values tracked per histogram slot per template; further distinct
    // values are counted in FieldHistogram::total and not stored individually.
    static constexpr std::size_t kDefaultMaxHistogramValues{64};
    std::size_t max_histogram_values{kDefaultMaxHistogramValues};

    [[nodiscard]] bool operator==(const MetaLogConfig&) const noexcept = default;
};

// invariant: the generation of the SALIENCE ARITHMETIC this producer implements -- the half of the
// retention profile that no configuration field can express.
// invariant: two documents produced under the same configuration tuple by two binaries whose
// ladders differ are not comparable, and only this constant can say so.
// invariant: any edit to the salience arithmetic that can move a retained bag for some input bumps
// this; the two near-full reservoir goldens are the observable that catches a miss.
// refs: ADR-31.D8
inline constexpr std::string_view kSalienceArithmeticGeneration{"salience-1"};

// post: the retention_profile stamp DERIVED from the parameters, never a hand-written literal that
// could drift from the retention the engine applied.
// invariant: INJECTIVE -- each axis is a one-letter tag plus a non-empty decimal run, joined by a
// character no decimal run contains, so the string determines the tuple.
// invariant: RECONSTRUCTABLE -- the retention a stored document was produced under is readable off
// the stamp alone, which is why it is a legible name and not a hash.
// invariant: deterministic across toolchains -- integer std::to_chars only, no locale, no float, no
// hashing.
// refs: F-SRC-metalog-spec:SPEC.md
[[nodiscard]] inline std::string retention_profile_name(const MetaLogConfig& config)
{
    // invariant: any std::size_t in base 10, plus its one-character axis tag.
    constexpr std::size_t kAxisFieldMax{std::numeric_limits<std::size_t>::digits10 + 2};
    std::string name{kSalienceArithmeticGeneration};
    const auto append_axis{
        [&name](char tag, std::size_t value)
        {
            std::array<char, kAxisFieldMax> digits{};
            const auto written{std::to_chars(digits.data(), digits.data() + digits.size(), value)};
            name.push_back(tag);
            name.append(digits.data(), written.ptr);
        }};
    name.push_back('/');
    append_axis('k', config.top_k_size);
    name.push_back('-');
    append_axis('m', config.reservoir_size);
    name.push_back('-');
    append_axis('c', config.reservoir_per_kind_cap);
    name.push_back('-');
    append_axis('e', config.reservoir_error_reserve);
    return name;
}

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

// invariant: an edge present on BOTH sides whose observed weight moved.
// refs: SRC-D-OTEL-21
struct ServiceEdgeWeightChange
{
    std::string caller;
    std::string callee;
    std::uint64_t previous_weight{0};
    std::uint64_t current_weight{0};
    // invariant: current minus previous.
    std::int64_t delta{0};
};

// invariant: its OWN diff pass over the two windows' service_edges blocks; emerged and vanished are
// the appeared-from-nothing and disappeared edge sets at the cube's absolute emergence discipline.
// invariant: semantics-free integer and set arithmetic -- metalog stays polarity-blind and the
// degraded reading is the consumer's.
// invariant: present ONLY when BOTH documents carried a service_edges block; absence means edge
// verdicts are unknown, never that all edges emerged.
// refs: SRC-D-OTEL-21, SRC-D-OTEL-22
struct ServiceEdgeDelta
{
    std::vector<ServiceEdge> emerged;
    std::vector<ServiceEdge> vanished;
    std::vector<ServiceEdgeWeightChange> weight_changed;
};

// invariant: the per-(template_id, param_index) JS divergence between two windows' value_counts
// distributions, populated only when both documents tracked histograms and share the template.
// invariant: js_divergence uses the same Laplace-smoothed log2 convention as
// MetaLogDiff::js_divergence -- bits in [0, 1], clamped.
struct FieldHistogramDelta
{
    TemplateId template_id;
    std::uint32_t param_index{0};
    double js_divergence{0.0};
    double previous_entropy_bits{0.0};
    double current_entropy_bits{0.0};
    // invariant: the per-side observation count backing each distribution -- the sample size the
    // divergence is estimated from, and the basis for a consumer's min-sample floor.
    // invariant: distinct from cardinality and from the template's stream share; it may exceed the
    // sum of value_counts when high-cardinality values were not retained individually.
    std::uint64_t previous_sample_count{0};
    std::uint64_t current_sample_count{0};
    // invariant: zero when either document provided no approximate_cardinality for this slot.
    std::uint64_t previous_cardinality{0};
    std::uint64_t current_cardinality{0};
    // invariant: current_cardinality minus previous_cardinality.
    std::int64_t cardinality_delta{0};
};

// invariant: carries BOTH sides' raw counts, totals and schedule ids; the eidos diff gates on the
// schedule ids matching, then computes the distance and the shift bucket.
// invariant: metalog carries the counts and eidos owns the distance, which is ladder-agnostic at
// w=1.
// invariant: populated only when the same (template_id, field_name) appears in BOTH documents'
// ordinal_histograms.
// refs: SRC-D-W1-1, SRC-D-W1-4
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

// invariant: the pairwise change in the bounded long-tail shape, present only when BOTH documents
// carried a tail_summary.
// invariant: every delta is current minus previous, so a negative entropy delta is a tail
// collapsing toward one dominant template and a positive max-rate delta is it growing.
// invariant: a one-sided tail is a tail appearing or vanishing, which the template-level new and
// vanished signals already express.
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

// invariant: one border cell is the constraint coordinate plus the (was, now) counts it bounds.
struct CubeBorderCell
{
    CubeCoord coord;
    std::uint64_t previous_count{0};
    std::uint64_t current_count{0};
    [[nodiscard]] bool operator==(const CubeBorderCell&) const noexcept = default;
};

// invariant: lower holds the most SPECIFIC emerging cells and upper the most GENERAL -- the minimal
// generators, which are the deterministic headline.
struct CubeBorder
{
    std::vector<CubeBorderCell> lower;
    std::vector<CubeBorderCell> upper;
    [[nodiscard]] bool operator==(const CubeBorder&) const noexcept = default;
};

// invariant: the emerging border between two cube blocks -- the smallest constraint characterising
// what GREW, and the dual for what vanished.
// invariant: the emerging region is defined by two ABSOLUTE thresholds and never a ratio, so it is
// order-convex and bounded by a (lower, upper) border pair.
// invariant: emitted only when BOTH documents carried a cube AND their axes are equal; axes equals
// both inputs' cube axes.
// refs: F-SRC-metalog-spec:SPEC.md
struct CubeDiffBlock
{
    std::vector<CubeAxis> axes;
    // invariant: a presence bool plus an inline value, NOT std::optional<CubeBorder>, because a
    // synthesized optional copy of a vector-owning type hits the same MSVC miscompile.
    bool has_emerging{false};
    // invariant: the growth region, valid iff has_emerging.
    CubeBorder emerging{};
    bool has_vanishing{false};
    // invariant: the disappearance region and the dual, valid iff has_vanishing.
    CubeBorder vanishing{};
    [[nodiscard]] bool operator==(const CubeDiffBlock&) const noexcept = default;
};

// invariant: the direction of an ERROR/FATAL failure-frontier crossing, oriented previous to
// current.
// invariant: SIGNED but POLARITY-MUTE -- the escalation or recovery reading is the consumer's,
// never a good or bad verdict here.
enum class FrontierDirection : std::uint8_t
{
    // invariant: crossed INTO the failure band.
    Up,
    // invariant: crossed OUT of the failure band.
    Down
};

// invariant: one rare-salient template on the reservoir-delta membership boundary.
// invariant: the snapshot is the entry as it stands on the side that OWNS it -- the current window
// for new_salient and the previous window for vanished_salient.
// invariant: drawn from a document's RESERVOIR, so every field is populated.
struct ReservoirDeltaEntry
{
    TemplateId template_id;
    // invariant: the level AND its provenance, because this member is a streaming decision signal
    // and a snapshot that dropped the marker would rebuild a claim from an invisible guess.
    // refs: DN-32.D3
    std::optional<EventLevel> dominant_level;
    StructuralRole structural_role{StructuralRole::None};
    std::uint32_t salience{0};
    std::uint64_t count{0};
    // invariant: the template's SHARE of the window that owns this snapshot, so a consumer can rank
    // the row without re-reading the documents.
    // invariant: domain-only -- the wire row does not carry it.
    // refs: DN-64.D4
    double frequency{0.0};
    // invariant: the snapshot's retention argmax, carried from the owning side's entry.
    // invariant: domain-only -- the wire row does not carry it.
    // refs: DN-64.D3
    std::optional<RetentionAxis> retention_axis;
    [[nodiscard]] bool operator==(const ReservoirDeltaEntry&) const noexcept = default;
};

// invariant: a template present in BOTH sides' salience memory whose dominant_level crosses the
// ERROR/FATAL frontier.
struct FrontierCrossing
{
    TemplateId template_id;
    FrontierDirection direction;
    // invariant: both sides carry their provenance, so a consumer can see whether the levels that
    // define the crossing were declared or inferred.
    // refs: DN-32.D3
    std::optional<EventLevel> previous_level;
    std::optional<EventLevel> current_level;
    // invariant: the two sides' occurrence counts and shares, from the salience-memory entry each
    // side owns, so a consumer attributes the crossing without re-reading the documents.
    // invariant: domain-only -- the wire row carries the ids and levels alone.
    // refs: DN-64.D4
    std::uint64_t previous_count{0};
    std::uint64_t current_count{0};
    double previous_frequency{0.0};
    double current_frequency{0.0};
    [[nodiscard]] bool operator==(const FrontierCrossing&) const noexcept = default;
};

// invariant: the delta over the two documents' salience memory, which is top_k union reservoir.
// invariant: new_salient is in current.reservoir and absent from previous memory, vanished_salient
// the mirror, and frontier_crossings sits in both.
// invariant: every list is keyed and sorted by template_id, and the arithmetic is set difference
// plus integer level compares over already-deterministic membership.
// invariant: new_salient and frontier_crossings are the STREAMING members; vanished_salient is the
// BATCH member and a streaming consumer MUST NOT alert on it.
// refs: ADR-31.D8
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

// invariant: the producer's ASSERTION about the comparison it performed, never a summary of which
// fields it chose to serialise.
// invariant: Changed obliges the document to carry a WITNESS -- one signal property that is
// non-vacuous by its own declaration in the diff schema.
// invariant: Unchanged forbids a witness; it is a POSITIVE result and MAY still carry signal
// properties as long as every one sits at its declared vacuous value.
// invariant: NOT a member of MetaLogDiff -- it is a pure function of the findings, derived at the
// wire seam so a hand-built diff cannot carry a default contradicting its content.
// refs: F-SRC-metalog-spec:SPEC.md
enum class ComparisonOutcome : std::uint8_t
{
    // invariant: the comparison ran and found no change; the document carries NO witness.
    Unchanged,
    // invariant: the comparison found at least one change; the document carries its witness.
    Changed
};

// invariant: the two wire tokens the specification fixes, exhaustive over the enum.
[[nodiscard]] inline std::string_view to_string(ComparisonOutcome outcome) noexcept
{
    return outcome == ComparisonOutcome::Changed ? "changed" : "unchanged";
}

struct MetaLogDiff
{
    // invariant: the version of the SPECIFICATION this document conforms to -- the same axis AND
    // the same value as metalog_version, never an independent version of the diff document.
    // invariant: a producer MUST NOT emit a version older than the version at which the newest
    // member it carries was minted.
    // invariant: no schema keyword can catch that, so the rule binds the producer and is held here.
    // refs: F-SRC-metalog-spec:SPEC.md
    std::string diff_version{kMetaLogSpecVersion};
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
    // invariant: present ONLY when both documents carried a service_edges block; absence reads
    // unknown.
    // invariant: serialised under the extensions container, which the witness derivation excludes
    // by name.
    // refs: SRC-D-OTEL-21
    std::optional<ServiceEdgeDelta> service_edge_delta;
    // invariant: empty unless both documents tracked histograms and share a template; sorted by
    // js_divergence descending.
    std::vector<FieldHistogramDelta> field_histogram_deltas;
    // invariant: empty unless both documents tracked histograms and share a (template_id,
    // declared-ordinal field); sorted by (template_id, field_name).
    // refs: SRC-D-W1-1
    std::vector<OrdinalHistogramDelta> ordinal_histogram_deltas;
    // invariant: present only when both documents carried a tail_summary.
    std::optional<TailDelta> tail_delta;
    // invariant: present only when both documents carried a cube and their axes are equal.
    // invariant: structured evidence -- the upper border is the deterministic headline, and it is
    // not an alert on its own.
    // invariant: a presence bool plus an inline value for MetaLogDocument::cube's reason, and
    // because axes is a required descriptor: present-but-empty is a state, absence is not.
    bool has_cube_diff{false};
    CubeDiffBlock cube_diff{};
    // invariant: additive on the DERIVED diff, so no canonicalization_version bump -- that axis is
    // canon's processing contract, which a derived diff block does not move.
    // invariant: an inline value, because emptiness IS absence here: all three lists empty means
    // the block is omitted from JSON, so a separate presence flag would be redundant state.
    ReservoirDelta reservoir_delta{};
};

} // namespace insight::metalog
