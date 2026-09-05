// invariant: the cube is a pure function of the closed window -- integer counts, set operations,
// prefix-truncated roll-up, coord-sorted output -- so it is bit-identical.
// refs: ADR-3.D4
export module insight.metalog.detail.cube;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;

export namespace insight::metalog::cube
{

inline constexpr std::size_t kNumDims{3};

// invariant: the fourth slot is uniformly kStar in a stored cube, so a stored cube is
// byte-identical to a three-dimensional one; only cube_diff_of ever pins it.
inline constexpr std::size_t kCellDims{4};

enum class Dim : std::uint8_t
{
    Level = 0,
    Where = 1,
    Role = 2,
    // note: the band is signed and polarity-mute -- up and down are distinct value-ids.
    LatencyShift = 3,
};

// invariant: kStar is the numeric max, so an unpinned dimension sorts last in the order.
inline constexpr std::uint32_t kStar{std::numeric_limits<std::uint32_t>::max()};

// invariant: emergence is absolute and never a ratio, so the border stays an up-set met with a
// down-set.
inline constexpr std::uint64_t kThetaWas{0};
inline constexpr std::uint64_t kThetaNow{1};

// invariant: a base observation pins every dimension except possibly Where, which an
// empty-component event leaves starred.
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
    // note: not = default: a defaulted friend operator== on an import-std type is a GNU defect.
    friend constexpr bool operator==(const Cell& lhs, const Cell& rhs) noexcept
    {
        return lhs.value == rhs.value;
    }
};

// post: the canonical total order -- pinned-mask first, then the value tuple, kStar last.
[[nodiscard]] constexpr bool cell_precedes(const Cell& lhs, const Cell& rhs) noexcept
{
    if (lhs.pinned_mask() != rhs.pinned_mask())
        return lhs.pinned_mask() < rhs.pinned_mask();
    return lhs.value < rhs.value;
}

// post: keeps the dims set in `mask` pinned to base's value and stars the rest; a dim already
// starred in `base` stays starred.
[[nodiscard]] constexpr Cell generalize(const Cell& base, std::uint8_t mask) noexcept
{
    Cell out{};
    for (std::size_t dim{0}; dim < kCellDims; ++dim)
        out.value[dim] = (((mask >> dim) & 1U) != 0U) ? base.value[dim] : kStar;
    return out;
}

// post: agreeing dims keep their value and differing dims star, so the running meet over a cell's
// base tuples is its closure.
[[nodiscard]] constexpr Cell meet(const Cell& lhs, const Cell& rhs) noexcept
{
    Cell out{};
    for (std::size_t dim{0}; dim < kCellDims; ++dim)
        out.value[dim] = (lhs.value[dim] == rhs.value[dim]) ? lhs.value[dim] : kStar;
    return out;
}

// post: true iff every pinned dim of `general` equals base's value.
// note: count(c) is the greatest count over the closed cells that c generalizes.
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

// invariant: closure == cell iff the cell is closed.
struct CellAggregate
{
    std::uint64_t count{0};
    Cell closure{};
};

// invariant: a PopulatedCube is a flat vector in cell_precedes order, which is the canonical order,
// so close, border and count_in read it directly.
// note: the reduce is order-independent -- counts sum, meet is commutative and associative.
struct PopulatedCell
{
    Cell cell;
    CellAggregate agg;
};
using PopulatedCube = std::vector<PopulatedCell>;

// invariant: an empty `component` means the event has no WHERE, so its base cell stars it.
struct BaseRow
{
    LogLevel level{LogLevel::Unknown};
    std::string_view component;
    StructuralRole role{StructuralRole::None};
    std::uint64_t count{0};
};

// post: the fixed reference axes, frozen per canonicalization_version and retention_profile and
// never adapted per window.
[[nodiscard]] std::vector<CubeAxis> reference_axes();

// post: false iff some prefix path appears under two parents; vacuously true at depth 1.
// note: a DAG there would break roll-up monotonicity and the border.
[[nodiscard]] bool where_chain_is_tree(std::span<const std::vector<std::string>> paths);

// post: throws std::logic_error when the constructed WHERE chains are not a single-parent tree.
[[nodiscard]] CubeBlock build_closed_cube(std::span<const BaseRow> base);

// post: a pure function of (level, component) carrying no salience and no role; an empty component
// yields no `where`.
[[nodiscard]] CubeCoord cube_location(std::optional<LogLevel> level, std::string_view component);

// pre: `current_shift_by_component` is the caller's per-component latency drift map; empty gives
// the plain three-dimensional border.
// post: emits containment -- the reference axes at the pair's minimal common collapse plus the
// latency_shift differential axis; this verb never refuses a pair.
// note: unequal axes are the mandated case: the diff reads the pair's minimal common collapse.
// refs: DN-42.D17
[[nodiscard]] CubeDiffBlock
cube_diff_of(const CubeBlock& previous, const CubeBlock& current,
             const std::unordered_map<std::string, OrdinalDrift>& current_shift_by_component = {});

// post: counts merge distributively and the cube is re-closed from the recovered base; the pair is
// rolled to its minimal common collapse rather than refused.
// refs: DN-42.D17
[[nodiscard]] CubeBlock compose_cubes(const CubeBlock& lhs, const CubeBlock& rhs);

} // namespace insight::metalog::cube
