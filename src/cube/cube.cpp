module;

module insight.metalog.detail.cube;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats; // level_to_spec_string

// MSVC-port opt-off (see metalog.api.impl.cpp): the cube diff/compose path also touches
// optional<CubeBlock>; disable optimization for this TU at the source level (CMake opt-flags
// don't reach module-unit compiles). Cold path; pure-integer → byte-identical output.
#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

// MetaLog intra-window cube (SPEC §16) + emerging-border cube_diff (§13.6). The closed
// cube is built ONCE in batch over the closed, frozen, ordered window, so it is a pure
// function of that set — bit-identical cross-stdlib and cross-OS (§16.9). The lattice
// primitives live in the detail.cube interface; this unit is the heavy machinery:
// closure, lossless base recovery, the order-convex border, and the compose re-closure.

namespace insight::metalog::cube
{
namespace
{

// ── enum ↔ wire-string inverses (round-trip a recovered cube) ───────────────────
// The cube value-ids are the canon enum casts (Level/Role) and the interned component
// index (Where). Recovering a cube from its DTO cells re-parses the wire strings back
// to those enums. cube_level folds Unknown→Info, so "INFO" → Info round-trips exactly.
[[nodiscard]] LogLevel level_from_spec(std::string_view spec) noexcept
{
    if (spec == "TRACE")
        return LogLevel::Trace;
    if (spec == "DEBUG")
        return LogLevel::Debug;
    if (spec == "WARN")
        return LogLevel::Warn;
    if (spec == "ERROR")
        return LogLevel::Error;
    if (spec == "FATAL")
        return LogLevel::Fatal;
    return LogLevel::Info; // "INFO" and any unknown spec
}

[[nodiscard]] StructuralRole role_from_string(std::string_view text) noexcept
{
    if (text == "GroupBegin")
        return StructuralRole::GroupBegin;
    if (text == "GroupEnd")
        return StructuralRole::GroupEnd;
    if (text == "Terminator")
        return StructuralRole::Terminator;
    return StructuralRole::None;
}

// Deterministic component value-id: index into the lexicographically sorted dictionary
// (binary search). kStar on empty / miss — an empty component is "no WHERE".
[[nodiscard]] std::uint32_t component_id(std::span<const std::string> labels,
                                         std::string_view component) noexcept
{
    if (component.empty())
        return kStar;
    const auto first{labels.begin()};
    const auto last{labels.end()};
    const auto found{std::lower_bound(first, last, component)};
    if (found != last && *found == component)
        return static_cast<std::uint32_t>(found - first);
    return kStar;
}

// ── coord ↔ internal cell ───────────────────────────────────────────────────────

// Internal Cell → wire coord, resolving the Where value-id back through the dictionary
// as a depth-1 chain ([component]). An absent (starred) dim → an absent coord key (§16.4).
[[nodiscard]] CubeCoord coord_of(const Cell& cell, std::span<const std::string> labels)
{
    CubeCoord coord;
    if (cell.pinned(Dim::Level))
        coord.level =
            level_to_spec_string(static_cast<LogLevel>(cell.value[static_cast<std::size_t>(Dim::Level)]));
    if (cell.pinned(Dim::Where))
    {
        const std::uint32_t where_id{cell.value[static_cast<std::size_t>(Dim::Where)]};
        coord.where = std::vector<std::string>{labels[where_id]};
    }
    if (cell.pinned(Dim::Role))
        coord.structural_role = std::string{
            to_string(static_cast<StructuralRole>(cell.value[static_cast<std::size_t>(Dim::Role)]))};
    return coord;
}

// Wire coord → internal Cell, interning the Where path's leaf (depth-1) through the
// SHARED dictionary so cells from two cubes compare on a common Where id space.
[[nodiscard]] Cell cell_of(const CubeCoord& coord, std::span<const std::string> labels) noexcept
{
    Cell cell;
    if (coord.level)
        cell.value[static_cast<std::size_t>(Dim::Level)] =
            static_cast<std::uint32_t>(level_from_spec(*coord.level));
    if (coord.where && !coord.where->empty())
        cell.value[static_cast<std::size_t>(Dim::Where)] = component_id(labels, coord.where->back());
    if (coord.structural_role)
        cell.value[static_cast<std::size_t>(Dim::Role)] =
            static_cast<std::uint32_t>(role_from_string(*coord.structural_role));
    return cell;
}

// The WHERE-path of a closed cell (depth-1 chain), for the MUST-1 tree validation.
[[nodiscard]] std::vector<std::vector<std::string>>
where_paths_of(const std::vector<CubeCell>& cells)
{
    std::vector<std::vector<std::string>> paths;
    for (const CubeCell& cell : cells)
        if (cell.coord.where && !cell.coord.where->empty())
            paths.push_back(*cell.coord.where);
    return paths;
}

// ── populate (the §16.4 single generalization pass) ─────────────────────────────
// Each base tuple folds itself into the counts + the closure-meet of every one of its
// 2^(pinned) generalizations. Iterating over subsets of the PINNED dims (not all 2^n
// masks) is what keeps an empty-component base (Where already starred) from being
// double-counted into the Where-aggregated cells. Order-independent integer set work.
[[nodiscard]] PopulatedCube populate(const std::map<Cell, std::uint64_t, CellLess>& base)
{
    PopulatedCube cube;
    for (const auto& [tuple, multiplicity] : base)
    {
        std::array<std::size_t, kNumDims> pinned_dims{};
        std::size_t pinned{0};
        for (std::size_t dim{0}; dim < kNumDims; ++dim)
            if (tuple.value[dim] != kStar)
                pinned_dims[pinned++] = dim;
        const std::uint32_t subsets{1U << pinned};
        for (std::uint32_t subset{0}; subset < subsets; ++subset)
        {
            Cell gen{};
            for (std::size_t i{0}; i < pinned; ++i)
                if (((subset >> i) & 1U) != 0U)
                    gen.value[pinned_dims[i]] = tuple.value[pinned_dims[i]];
            CellAggregate& agg{cube[gen]};
            const bool seeded{agg.count != 0};
            agg.count += multiplicity;
            agg.closure = seeded ? meet(agg.closure, tuple) : tuple;
        }
    }
    return cube;
}

[[nodiscard]] std::uint64_t count_in(const PopulatedCube& cube, const Cell& cell) noexcept
{
    const auto found{cube.find(cell)};
    return found == cube.end() ? std::uint64_t{0} : found->second.count;
}

// ── lossless base recovery from closed cells (§16.4) ────────────────────────────
// count(c) = max{ count(c*) : c* closed, is_ancestor(c, c*) } — the closure regenerates
// every cell. Used only to recover the base multiset for diff/compose (the document
// stores closed cells, not the base).
struct InternalCell
{
    Cell cell;
    std::uint64_t count{0};
};

[[nodiscard]] std::uint64_t count_in_closed(std::span<const InternalCell> closed,
                                            const Cell& cell) noexcept
{
    std::uint64_t best{0};
    for (const InternalCell& closed_cell : closed)
        if (is_ancestor(cell, closed_cell.cell) && closed_cell.count > best)
            best = closed_cell.count;
    return best;
}

// Recover the base multiset: the fully-pinned closed cells are the non-empty-component
// observations directly; each (level, role) carries an empty-component residual
// = count((L,*,R)) − Σ_w count((L,w,R)), an empty-WHERE base tuple (Where starred).
[[nodiscard]] std::map<Cell, std::uint64_t, CellLess>
recover_base(std::span<const InternalCell> closed)
{
    std::map<Cell, std::uint64_t, CellLess> base;
    for (const InternalCell& entry : closed)
        if (entry.cell.pinned_count() == kNumDims)
            base[entry.cell] += entry.count;

    // Distinct (Level, Role) pairs that pin both dims — candidates for an empty-WHERE residual.
    std::set<std::pair<std::uint32_t, std::uint32_t>> level_role;
    for (const InternalCell& entry : closed)
        if (entry.cell.pinned(Dim::Level) && entry.cell.pinned(Dim::Role))
            level_role.emplace(entry.cell.value[static_cast<std::size_t>(Dim::Level)],
                               entry.cell.value[static_cast<std::size_t>(Dim::Role)]);

    for (const auto& [level_id, role_id] : level_role)
    {
        Cell star_where;
        star_where.value[static_cast<std::size_t>(Dim::Level)] = level_id;
        star_where.value[static_cast<std::size_t>(Dim::Role)] = role_id;
        // Where stays kStar.
        const std::uint64_t total{count_in_closed(closed, star_where)};
        std::uint64_t pinned_sum{0};
        for (const InternalCell& entry : closed)
            if (entry.cell.pinned_count() == kNumDims &&
                entry.cell.value[static_cast<std::size_t>(Dim::Level)] == level_id &&
                entry.cell.value[static_cast<std::size_t>(Dim::Role)] == role_id)
                pinned_sum += entry.count;
        if (total > pinned_sum)
            base[star_where] += total - pinned_sum;
    }
    return base;
}

[[nodiscard]] std::vector<InternalCell> internal_cells(const CubeBlock& block,
                                                       std::span<const std::string> labels)
{
    std::vector<InternalCell> out;
    out.reserve(block.cells.size());
    for (const CubeCell& cell : block.cells)
        out.push_back(InternalCell{.cell = cell_of(cell.coord, labels), .count = cell.count});
    return out;
}

// The shared component dictionary = the sorted union of both cubes' WHERE leaves.
[[nodiscard]] std::vector<std::string> shared_labels(const CubeBlock& lhs, const CubeBlock& rhs)
{
    std::set<std::string> uniq;
    for (const CubeBlock* block : {&lhs, &rhs})
        for (const CubeCell& cell : block->cells)
            if (cell.coord.where && !cell.coord.where->empty())
                uniq.insert(cell.coord.where->back());
    return {uniq.begin(), uniq.end()};
}

// Emit the closed cells of a populated cube as the wire block (canonical coord-sorted
// order — the PopulatedCube map already iterates in CellLess order). raw_cell_count =
// every populated cell; cell_count = the closed ones (closure == cell). MUST-1: the
// constructed WHERE chains MUST be a single-parent tree.
[[nodiscard]] CubeBlock close_and_emit(const std::map<Cell, std::uint64_t, CellLess>& base,
                                       std::span<const std::string> labels)
{
    const PopulatedCube cube{populate(base)};
    CubeBlock block;
    block.axes = reference_axes();
    block.raw_cell_count = cube.size();
    for (const auto& [cell, agg] : cube)
        if (agg.closure == cell) // closed
            block.cells.push_back(CubeCell{.coord = coord_of(cell, labels), .count = agg.count});
    block.cell_count = block.cells.size();

    const std::vector<std::vector<std::string>> paths{where_paths_of(block.cells)};
    if (!where_chain_is_tree(paths))
        throw std::logic_error{
            "metalog::cube: WHERE chain is not a single-parent tree (SPEC §16.5 MUST-1)"};
    return block;
}

// ── the order-convex border (§13.6) ─────────────────────────────────────────────
// One growth/disappearance region: emergent(c) = count(anti, c) ≤ θ_was ∧
// count(mono, c) ≥ θ_now (the two ABSOLUTE thresholds, §16.5 MUST-2). The region is an
// up-set ∩ down-set, so a cell is on the UPPER border iff it has no emergent parent
// (un-pinning leaves the region) and on the LOWER border iff it has no emergent child.
[[nodiscard]] CubeBorder border_of(const PopulatedCube& anti, const PopulatedCube& mono,
                                   const PopulatedCube& previous, const PopulatedCube& current,
                                   std::span<const std::string> labels)
{
    const auto emergent{[&](const Cell& cell) noexcept
                        { return count_in(anti, cell) <= kThetaWas && count_in(mono, cell) >= kThetaNow; }};

    // Every emergent cell has count(mono) ≥ θ_now ≥ 1, so it is a key of `mono`.
    std::vector<Cell> cells;
    std::map<Cell, std::size_t, CellLess> index;
    for (const auto& [cell, agg] : mono)
        if (emergent(cell))
        {
            index.emplace(cell, cells.size());
            cells.push_back(cell);
        }

    std::vector<bool> has_parent(cells.size(), false);
    std::vector<bool> has_child(cells.size(), false);
    for (std::size_t i{0}; i < cells.size(); ++i)
        for (std::size_t dim{0}; dim < kNumDims; ++dim)
        {
            if (!cells[i].pinned(static_cast<Dim>(dim)))
                continue;
            Cell parent{cells[i]};
            parent.value[dim] = kStar; // un-pin one dim = one step toward the apex
            const auto found{index.find(parent)};
            if (found == index.end())
                continue; // parent not emergent
            has_parent[i] = true;
            has_child[found->second] = true; // the parent has an emergent child (cells[i])
        }

    const auto border_cell{[&](const Cell& cell)
                           {
                               return CubeBorderCell{.coord = coord_of(cell, labels),
                                                     .previous_count = count_in(previous, cell),
                                                     .current_count = count_in(current, cell)};
                           }};
    // `cells` was populated by iterating `mono` (a CellLess-ordered map), so it is
    // already in canonical cell order; upper/lower inherit that order — no re-sort needed.
    CubeBorder out;
    for (std::size_t i{0}; i < cells.size(); ++i)
    {
        if (!has_parent[i])
            out.upper.push_back(border_cell(cells[i])); // minimal generators = the headline
        if (!has_child[i])
            out.lower.push_back(border_cell(cells[i])); // most-specific = the precise description
    }
    return out;
}

} // namespace

std::vector<CubeAxis> reference_axes()
{
    // Canonical key order: Level, Where, Role (Dim order). WHERE is a depth-1 chain in
    // v0.6.0 (grounded in canon `component`); the chain + floor_depth carry the roll-up
    // mechanism so 1.5.5's dimensional-shrink is a content change, not a schema change.
    CubeAxis level{.name = "level", .kind = "categorical", .chain = std::nullopt, .floor_depth = std::nullopt};
    CubeAxis where{.name = "where",
                   .kind = "chain",
                   .chain = std::vector<std::string>{"component"},
                   .floor_depth = std::uint32_t{1}};
    CubeAxis role{
        .name = "structural_role", .kind = "categorical", .chain = std::nullopt, .floor_depth = std::nullopt};
    return {std::move(level), std::move(where), std::move(role)};
}

bool where_chain_is_tree(std::span<const std::vector<std::string>> paths)
{
    // A WHERE node is identified by its (depth, value); its parent is the value at the
    // previous depth (the empty sentinel at depth 0 = the virtual root). The chain is a
    // single-parent tree iff no value-node ever appears under two different parents — a
    // value with two parents is a DAG, which breaks prefix-truncation roll-up (a finer
    // node would roll up to two coarser nodes → double-counting → the border ill-defined).
    // At depth 1 (the v0.6.0 regime) every component is a child of the root, so a flat
    // single-level chain is always a tree.
    std::map<std::pair<std::size_t, std::string>, std::string> parent_of;
    for (const std::vector<std::string>& path : paths)
        for (std::size_t depth{0}; depth < path.size(); ++depth)
        {
            const std::string parent{depth == 0 ? std::string{} : path[depth - 1]};
            const auto [iter, inserted]{
                parent_of.try_emplace(std::pair{depth, path[depth]}, parent)};
            if (!inserted && iter->second != parent)
                return false; // this value already has a different parent → multi-parent (a DAG)
        }
    return true;
}

CubeBlock build_closed_cube(std::span<const BaseRow> base_rows)
{
    // The shared component dictionary for this window = sorted unique non-empty components.
    std::set<std::string> uniq;
    for (const BaseRow& row : base_rows)
        if (!row.component.empty())
            uniq.emplace(row.component);
    const std::vector<std::string> labels{uniq.begin(), uniq.end()};

    std::map<Cell, std::uint64_t, CellLess> base;
    for (const BaseRow& row : base_rows)
    {
        Cell cell;
        cell.value[static_cast<std::size_t>(Dim::Level)] =
            static_cast<std::uint32_t>(cube_level(row.level));
        cell.value[static_cast<std::size_t>(Dim::Where)] = component_id(labels, row.component);
        cell.value[static_cast<std::size_t>(Dim::Role)] = static_cast<std::uint32_t>(row.role);
        base[cell] += row.count;
    }
    return close_and_emit(base, labels);
}

CubeCoord cube_location(std::optional<LogLevel> level, std::string_view component)
{
    CubeCoord coord; // LOCATION only: level + where, never role, never salience (§16.6)
    if (level)
        coord.level = level_to_spec_string(cube_level(*level));
    if (!component.empty())
        coord.where = std::vector<std::string>{std::string{component}};
    return coord;
}

std::optional<CubeDiffBlock> cube_diff_of(const CubeBlock& previous, const CubeBlock& current)
{
    // §13.6 comparability gate: emit only when the axes match. The "both documents carried a
    // cube" presence-check is the CALLER's (metalog::diff, in the insight.metalog module) — this
    // helper takes CubeBlock by ref, never the optional (MSVC miscompiles a `!optional<CubeBlock>`
    // null-check done here in the detail.cube module — see metalog.detail.cube.cppm).
    if (previous.axes != current.axes)
        return std::nullopt;

    const std::vector<std::string> labels{shared_labels(previous, current)};
    const std::map<Cell, std::uint64_t, CellLess> base_prev{
        recover_base(internal_cells(previous, labels))};
    const std::map<Cell, std::uint64_t, CellLess> base_cur{
        recover_base(internal_cells(current, labels))};
    const PopulatedCube cube_prev{populate(base_prev)};
    const PopulatedCube cube_cur{populate(base_cur)};

    CubeDiffBlock diff;
    diff.axes = previous.axes;
    // emerging: appeared (anti = prev, monotone = cur). vanishing: the dual via the role
    // swap (anti = cur, monotone = prev) — same predicate, count_cur ≤ θ_was ∧ count_prev ≥ θ_now.
    CubeBorder emerging{border_of(cube_prev, cube_cur, cube_prev, cube_cur, labels)};
    CubeBorder vanishing{border_of(cube_cur, cube_prev, cube_prev, cube_cur, labels)};
    if (!emerging.lower.empty() || !emerging.upper.empty())
        diff.emerging = std::move(emerging);
    if (!vanishing.lower.empty() || !vanishing.upper.empty())
        diff.vanishing = std::move(vanishing);
    return diff;
}

std::optional<CubeBlock> compose_cubes(const CubeBlock& lhs, const CubeBlock& rhs)
{
    // §12.1: re-closed, not merged cell-by-cell. Axes-mismatch → omit. The "both present, else
    // omit (§16.7)" presence-check is the CALLER's (metalog::compose) — takes CubeBlock by ref.
    if (lhs.axes != rhs.axes)
        return std::nullopt;

    const std::vector<std::string> labels{shared_labels(lhs, rhs)};
    std::map<Cell, std::uint64_t, CellLess> merged{recover_base(internal_cells(lhs, labels))};
    for (const auto& [cell, count] : recover_base(internal_cells(rhs, labels)))
        merged[cell] += count;
    return close_and_emit(merged, labels);
}

} // namespace insight::metalog::cube
