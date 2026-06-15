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
inline constexpr std::size_t kNumDims{3};
enum class Dim : std::uint8_t
{
    Level = 0,
    Where = 1,
    Role = 2,
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
    std::array<std::uint32_t, kNumDims> value{kStar, kStar, kStar};

    [[nodiscard]] constexpr bool pinned(Dim dim) const noexcept
    {
        return value[static_cast<std::size_t>(dim)] != kStar;
    }
    [[nodiscard]] constexpr std::uint8_t pinned_mask() const noexcept
    {
        std::uint8_t mask{0};
        for (std::size_t i{0}; i < kNumDims; ++i)
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
    for (std::size_t dim{0}; dim < kNumDims; ++dim)
        out.value[dim] = ((mask >> dim) & 1U) ? base.value[dim] : kStar;
    return out;
}

// meet(a, b): agree → keep value, differ → star. The closure of a cell is the running
// meet over the base tuples that map to it (§16.4 — count == cell ⇔ the cell is closed).
[[nodiscard]] constexpr Cell meet(const Cell& lhs, const Cell& rhs) noexcept
{
    Cell out{};
    for (std::size_t dim{0}; dim < kNumDims; ++dim)
        out.value[dim] = (lhs.value[dim] == rhs.value[dim]) ? lhs.value[dim] : kStar;
    return out;
}

// is_ancestor(general, base): does `general` generalize `base`? — every pinned dim of
// `general` equals base's value. Used to read count(c) off the closed cells:
// count(c) = max{ count(c*) : c* closed, is_ancestor(c, c*) } (§16.4 lossless recovery).
[[nodiscard]] constexpr bool is_ancestor(const Cell& general, const Cell& base) noexcept
{
    for (std::size_t dim{0}; dim < kNumDims; ++dim)
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
using PopulatedCube = std::map<Cell, CellAggregate, CellLess>;

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

// Emerging-border diff of two cube blocks (§13.6). nullopt unless BOTH carried a cube
// AND their axes are equal (the comparability gate). The upper border is the
// deterministic headline (minimal generators); the lower border the precise
// description. `vanishing` is the dual.
[[nodiscard]] std::optional<CubeDiffBlock> cube_diff_of(const std::optional<CubeBlock>& previous,
                                                        const std::optional<CubeBlock>& current);

// Compose two cube blocks (§16.7 / §12.1): the distributive counts merge, but the
// closure does NOT — the merged cube is RE-CLOSED from the recovered base. nullopt
// unless BOTH carried a cube AND their axes are equal (§12.1: when either omits, omit).
[[nodiscard]] std::optional<CubeBlock> compose_cubes(const std::optional<CubeBlock>& lhs,
                                                     const std::optional<CubeBlock>& rhs);

} // namespace insight::metalog::cube
