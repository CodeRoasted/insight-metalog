// insight.metalog.detail.cube — SEALED cube domain (domain decomposition, §11.9.11).
// The intra-window closed cube (SPEC §16) and its emerging-border diff (§13.6): the
// lattice primitives (Cell / generalize / meet / closure), the closed-cube builder,
// the order-convex (lower, upper) border, the compose re-closure, and the §16.6
// reservoir→cell LOCATION cross. A leaf over api + canon + detail.stats (for the spec
// level string) — imports no other metalog detail shard beyond stats. Never
// re-exported by the facade, never installed (PRIVATE file set).
//
// Determinism (§16.9): the cube is a PURE FUNCTION of the closed (frozen, ordered)
// window — `count` is integer, the closure and the border are set operations, the
// WHERE roll-up is prefix truncation only (no float→int anywhere), and cells/border
// cells serialise in canonical (coord-sorted) order. Bit-identical cross-stdlib and
// cross-OS, by construction.
export module insight.metalog.detail.cube;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats; // level_to_spec_string

export namespace insight::metalog::cube
{

// ── Fixed schema (§16.2/§16.4) ─────────────────────────────────────────────────
// The v0.6.0 reference axes, in the canonical key order Level=0, Where=1, Role=2.
// WHERE is a single-level chain in v0.6.0 (grounded in canon `component`); the chain
// representation carries the roll-up mechanism so the 1.5.5 dimensional-shrink lands
// with NO schema change (floor_depth shrinks, cells roll up — §16.3).
// The STORED cube's dimension count (Level, Where, Role) — the reference-axes count and the
// "fully-pinned base observation" arity (recover_base). UNCHANGED at 3: the stored cube stays 3D.
inline constexpr std::size_t kNumDims{3};

// The physical Cell width: kNumDims stored dims + ONE diff-only differential dimension
// (Dim::LatencyShift, cube_differential_axes.md §4). The shift axis exists ONLY inside
// cube_diff_of; in a STORED cube its slot is UNIFORMLY kStar (unpinned), so every stored cube is
// byte-identical to the 3-dim cube (a uniformly-star slot never pins, never generalizes, and —
// being the max value-id — never distinguishes the §16.4 canonical order). The lattice primitives
// iterate kCellDims so the diff can pin the shift; the stored build simply never sets it.
inline constexpr std::size_t kCellDims{4};

enum class Dim : std::uint8_t
{
    Level = 0,
    Where = 1,
    Role = 2,
    // Diff-only differential dimension (§4). Uniformly kStar in a stored cube; pinned in cube_diff
    // for a component whose latency (DurationLog2Ns) distribution shifted in EITHER direction
    // (≥LOW) — a SIGNED, polarity-mute band (up_* / down_*, §7.4). The A-side (baseline) projection
    // is uniformly SHIFT_NONE ≡ kStar, so a NONE→≥LOW crossing is an ordinary Emerging-Border
    // emergence with the shift as a 4th diff-only axis.
    LatencyShift = 3,
};

// '*' ("any") sentinel for an unpinned dimension's value-id; numeric max so it sorts
// LAST in the canonical order (§16.4). Level/Role value-ids are the canon enum casts;
// the Where value-id is the interned component index (kStar = empty component = the
// event has no WHERE → that dimension is aggregated for it).
inline constexpr std::uint32_t kStar{std::numeric_limits<std::uint32_t>::max()};

// Emergence thresholds (§16.5 MUST-2) — ABSOLUTE, never a ratio. Appeared-from-nothing
// (count_prev ≤ 0 ∧ count_cur ≥ 1) and its dual. Frozen constants: the border
// structure is well-defined only because emergence is an up-set ∩ down-set.
inline constexpr std::uint64_t kThetaWas{0};
inline constexpr std::uint64_t kThetaNow{1};

// A cell fixes each dimension to a concrete value-id or kStar. A base observation is a
// cell with no kStar EXCEPT possibly Where (an empty-component event has no WHERE).
struct Cell
{
    std::array<std::uint32_t, kCellDims> value{kStar, kStar, kStar, kStar};

    [[nodiscard]] constexpr bool pinned(Dim dim) const noexcept
    {
        return value[static_cast<std::size_t>(dim)] != kStar;
    }
    [[nodiscard]] constexpr std::uint8_t pinned_mask() const noexcept
    {
        std::uint8_t mask{0};
        for (std::size_t i{0}; i < kCellDims; ++i)
            if (value[i] != kStar)
                mask = static_cast<std::uint8_t>(mask | (1U << i));
        return mask;
    }
    [[nodiscard]] constexpr std::size_t pinned_count() const noexcept
    {
        return static_cast<std::size_t>(std::popcount(pinned_mask()));
    }
    // Value equality is tuple equality (pinned_mask is derived from it). A friend, so
    // gcc-15's mangler does not choke on a member operator in a module (same shape as
    // the cube package's Cell).
    friend constexpr bool operator==(const Cell& lhs, const Cell& rhs) noexcept
    {
        return lhs.value == rhs.value;
    }
};

// The canonical total order (§16.4): (pinned-mask, value0, value1, value2), kStar last.
[[nodiscard]] constexpr bool cell_precedes(const Cell& lhs, const Cell& rhs) noexcept
{
    if (lhs.pinned_mask() != rhs.pinned_mask())
        return lhs.pinned_mask() < rhs.pinned_mask();
    return lhs.value < rhs.value; // std::array op< is lexicographic; kStar (=max) sorts last
}

// generalize(base, mask): keep the dims set in `mask` pinned to base's value, star the
// rest — one of base's 2^(pinned) generalizations (§16.3 roll-up = prefix truncation,
// here generalised to the flat lattice; a dim already starred in `base` stays starred).
[[nodiscard]] constexpr Cell generalize(const Cell& base, std::uint8_t mask) noexcept
{
    Cell out{};
    for (std::size_t dim{0}; dim < kCellDims; ++dim)
        out.value[dim] = (((mask >> dim) & 1U) != 0U) ? base.value[dim] : kStar;
    return out;
}

// meet(a, b): agree → keep value, differ → star. The closure of a cell is the running
// meet over the base tuples that map to it (§16.4 — count == cell ⇔ the cell is closed).
[[nodiscard]] constexpr Cell meet(const Cell& lhs, const Cell& rhs) noexcept
{
    Cell out{};
    for (std::size_t dim{0}; dim < kCellDims; ++dim)
        out.value[dim] = (lhs.value[dim] == rhs.value[dim]) ? lhs.value[dim] : kStar;
    return out;
}

// is_ancestor(general, base): does `general` generalize `base`? — every pinned dim of
// `general` equals base's value. Used to read count(c) off the closed cells:
// count(c) = max{ count(c*) : c* closed, is_ancestor(c, c*) } (§16.4 lossless recovery).
[[nodiscard]] constexpr bool is_ancestor(const Cell& general, const Cell& base) noexcept
{
    for (std::size_t dim{0}; dim < kCellDims; ++dim)
        if (general.value[dim] != kStar && general.value[dim] != base.value[dim])
            return false;
    return true;
}

struct CellLess
{
    [[nodiscard]] bool operator()(const Cell& lhs, const Cell& rhs) const noexcept
    {
        return cell_precedes(lhs, rhs);
    }
};

// One populated cell's aggregate: integer COUNT + the meet over its base rows
// (== closure(c)). closure == cell ⇔ the cell is closed.
struct CellAggregate
{
    std::uint64_t count{0};
    Cell closure{};
};

// One populated cell = its coordinate + aggregate. Lever B: PopulatedCube is a FLAT vector sorted
// by cell_precedes — built by populate()'s collect→sort-once→linear-reduce, not an RB-tree of
// B·2ⁿ pointer-chasing inserts. The sort IS the §16.4 canonical order, so close/border iterate it
// directly and count_in binary-searches it. Reduce is order-independent (COUNT sums, `meet` is
// commutative+associative) ⇒ an unstable sort stays bit-identical.
// [[cube-reclosure-rework-inmem-wire-split]]
struct PopulatedCell
{
    Cell cell;
    CellAggregate agg;
};
using PopulatedCube = std::vector<PopulatedCell>;

// ── Severity normalisation ─────────────────────────────────────────────────────
// Fold LogLevel::Unknown → Info so the Level value-id is bijective with its spec
// string (level_to_spec_string maps BOTH Unknown and Info to "INFO"); without this,
// two distinct value-ids would serialise to one coord (a duplicate cell). The cube is
// built on the normalised level; the wire string is level_to_spec_string of it.
[[nodiscard]] inline LogLevel cube_level(LogLevel level) noexcept
{
    return level == LogLevel::Unknown ? LogLevel::Info : level;
}

// ── Base observation (the engine's per-window joint) ────────────────────────────
// One (level, component, role) joint with its multiplicity. An empty `component`
// means the event has no WHERE → its base cell stars the Where dim (§16.4 aggregated).
struct BaseRow
{
    LogLevel level{LogLevel::Unknown};
    std::string_view component;
    StructuralRole role{StructuralRole::None};
    std::uint64_t count{0};
};

// ── Public operations ──────────────────────────────────────────────────────────

// The fixed v0.6.0 reference axes (level categorical, structural_role categorical,
// where chain over ["component"] at floor_depth 1). Frozen per
// (canonicalization_version, retention_profile) — never adapted per window (§16.2).
[[nodiscard]] std::vector<CubeAxis> reference_axes();

// MUST-1 (§16.5): a WHERE chain MUST be a single-parent prefix-tree. Returns false if
// any node (a prefix path[0..d]) appears under two different parents (a DAG) — which
// breaks roll-up monotonicity and the border. Vacuously true for depth-1 chains.
[[nodiscard]] bool where_chain_is_tree(std::span<const std::vector<std::string>> paths);

// Build the CLOSED cube (§16.4) from the window's base joint. Throws std::logic_error
// if the constructed WHERE chains are not a tree (MUST-1) — a producer guard.
[[nodiscard]] CubeBlock build_closed_cube(std::span<const BaseRow> base);

// The §16.6 reservoir→cell LOCATION cross: a pure function of the entry's (level,
// component) → { level, where }. Carries NO salience and NO role (LOCATION = severity
// + where). Empty component → no `where` (the entry's WHERE is unknown).
[[nodiscard]] CubeCoord cube_location(std::optional<LogLevel> level, std::string_view component);

// Emerging-border diff of two cube blocks (§13.6). nullopt unless their axes are equal
// (the comparability gate). The upper border is the deterministic headline (minimal
// generators); the lower border the precise description. `vanishing` is the dual.
// The "both documents carried a cube" presence-check is the CALLER's job (`metalog::diff`
// gates on has_cube) — this helper takes CubeBlock by ref and owns only the axes-equality
// gate. (The cube DTOs are bool+value, not optional<…>, since MSVC miscompiles synthesized
// optional copies of the vector-owning cube types. [[msvc-port-stdlib-isms]])
//
// `current_shift_by_component` is the diff-time latency_shift dimension (cube_differential_axes.md
// §4): a per-component latency drift map the CALLER computes from the two documents' ordinal
// histograms (metalog::diff). BIDIRECTIONAL and polarity-MUTE — the drift carries a magnitude AND a
// direction (up/down); the CURRENT projection pins Dim::LatencyShift to a SIGNED band (up_* /
// down_*, distinct value-ids) for a component that shifted EITHER way, and the BASELINE stays
// uniformly SHIFT_NONE (kStar). So a cell leaving NONE in either direction emerges; up-cells and
// down-cells are distinct cells; metalog judges neither good nor bad (the reading layer, eidos,
// maps up→regression, down→recovery). The signed axis is a flat categorical axis to the border
// (each band a distinct pinned value, kStar the aggregated NONE center) → the order-convex
// (lower,upper) border stays monotone by the §A4 proof exactly as for level/role. latency_shift is
// EMERGENT-AT-DIFF: it has no stored-cube domain (never in reference_axes, so compare-at-min —
// which reads only level/where off the stored inputs — never compares it diff-vs-state). When empty
// (the default — no comparable ordinal data, or nothing shifted), the diff is the plain 3-D border,
// byte-identical to before.
[[nodiscard]] std::optional<CubeDiffBlock>
cube_diff_of(const CubeBlock& previous, const CubeBlock& current,
             const std::unordered_map<std::string, OrdinalDrift>& current_shift_by_component = {});

// Compose two cube blocks (§16.7 / §12.1): the distributive counts merge, but the
// closure does NOT — the merged cube is RE-CLOSED from the recovered base. nullopt
// unless their axes are equal. As with cube_diff_of, the "both present, else omit"
// presence-check is the CALLER's job (in `metalog::compose`); takes CubeBlock by ref.
[[nodiscard]] std::optional<CubeBlock> compose_cubes(const CubeBlock& lhs, const CubeBlock& rhs);

} // namespace insight::metalog::cube
