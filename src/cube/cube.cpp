module;

module insight.metalog.detail.cube;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;

namespace insight::metalog::cube
{
namespace
{

    // post: the exact inverse of level_to_spec_string over the seven LogLevel members, so every
    // emitted coord round-trips.
    // note: a token outside the vocabulary reads as Unknown, not as an invented severity.
    [[nodiscard]] LogLevel level_from_spec(std::string_view spec) noexcept
    {
        if (spec == "TRACE")
            return LogLevel::Trace;
        if (spec == "DEBUG")
            return LogLevel::Debug;
        if (spec == "INFO")
            return LogLevel::Info;
        if (spec == "WARN")
            return LogLevel::Warn;
        if (spec == "ERROR")
            return LogLevel::Error;
        if (spec == "FATAL")
            return LogLevel::Fatal;
        return LogLevel::Unknown;
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

    // post: the index into the lexicographically sorted dictionary, or kStar for an empty or
    // unknown component.
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

    // invariant: NONE is kStar; an up band is its magnitude and a down band is magnitude plus
    // kMagnitudeBands, so up and down are distinct value-ids.
    // note: the border reads the slot as flat categorical and never uses its value order.
    inline constexpr std::uint32_t kMagnitudeBands{3};

    [[nodiscard]] std::uint32_t signed_shift_id(OrdinalShift shift,
                                                OrdinalDriftDirection direction) noexcept
    {
        const std::uint32_t magnitude{static_cast<std::uint32_t>(shift)};
        return direction == OrdinalDriftDirection::Down ? magnitude + kMagnitudeBands : magnitude;
    }

    // pre: `band_id` is a real band -- NONE is never pinned, it is the kStar baseline.
    // post: the mute wire label oriented previous to current: up means current shifted higher.
    [[nodiscard]] std::string signed_shift_label(std::uint32_t band_id)
    {
        const bool down{band_id > kMagnitudeBands};
        const auto magnitude{static_cast<OrdinalShift>(down ? band_id - kMagnitudeBands : band_id)};
        std::string label{down ? "down_" : "up_"};
        label.append(to_string(magnitude));
        return label;
    }

    // post: a starred dimension yields an absent coord key.
    [[nodiscard]] CubeCoord coord_of(const Cell& cell, std::span<const std::string> labels)
    {
        CubeCoord coord;
        if (cell.pinned(Dim::Level))
            coord.level = level_to_spec_string(
                static_cast<LogLevel>(cell.value[static_cast<std::size_t>(Dim::Level)]));
        if (cell.pinned(Dim::Where))
        {
            const std::uint32_t where_id{cell.value[static_cast<std::size_t>(Dim::Where)]};
            coord.where = std::vector<std::string>{labels[where_id]};
        }
        if (cell.pinned(Dim::Role))
            coord.structural_role = std::string{to_string(
                static_cast<StructuralRole>(cell.value[static_cast<std::size_t>(Dim::Role)]))};
        // assert: the shift slot only ever holds a signed band, so a stored coord omits the key.
        if (cell.pinned(Dim::LatencyShift))
            coord.latency_shift =
                signed_shift_label(cell.value[static_cast<std::size_t>(Dim::LatencyShift)]);
        return coord;
    }

    // pre: `labels` is the shared dictionary, so two cubes' cells compare on one Where id space.
    [[nodiscard]] Cell cell_of(const CubeCoord& coord, std::span<const std::string> labels) noexcept
    {
        Cell cell;
        if (coord.level)
            cell.value[static_cast<std::size_t>(Dim::Level)] =
                static_cast<std::uint32_t>(level_from_spec(*coord.level));
        if (coord.where && !coord.where->empty())
            cell.value[static_cast<std::size_t>(Dim::Where)] =
                component_id(labels, coord.where->back());
        if (coord.structural_role)
            cell.value[static_cast<std::size_t>(Dim::Role)] =
                static_cast<std::uint32_t>(role_from_string(*coord.structural_role));
        return cell;
    }

    // post: the WHERE path of every closed cell that has one, for the tree validation.
    [[nodiscard]] std::vector<std::vector<std::string>>
    where_paths_of(const std::vector<CubeCell>& cells)
    {
        std::vector<std::vector<std::string>> paths;
        for (const CubeCell& cell : cells)
            if (cell.coord.where && !cell.coord.where->empty())
                paths.push_back(*cell.coord.where);
        return paths;
    }

    // post: each base tuple folds into the counts and the closure-meet of every one of its 2^pinned
    // generalizations.
    // note: only PINNED dims are subsetted, so a starred Where is never double-counted.
    [[nodiscard]] PopulatedCube populate(std::span<const std::pair<Cell, std::uint64_t>> base)
    {
        // invariant: the reduce is order-independent -- counts sum, meet is commutative and
        // associative -- so the unstable sort is bit-identical and the sort IS the canonical order.
        struct GenEvent
        {
            Cell gen;
            std::uint64_t mult;
            Cell src;
        };
        std::vector<GenEvent> events;
        events.reserve(base.size() * (std::size_t{1} << kCellDims));
        for (const auto& [tuple, multiplicity] : base)
        {
            std::array<std::size_t, kCellDims> pinned_dims{};
            std::size_t pinned{0};
            for (std::size_t dim{0}; dim < kCellDims; ++dim)
                if (tuple.value[dim] != kStar)
                    pinned_dims[pinned++] = dim;
            const std::uint32_t subsets{1U << pinned};
            for (std::uint32_t subset{0}; subset < subsets; ++subset)
            {
                Cell gen{};
                for (std::size_t i{0}; i < pinned; ++i)
                    if (((subset >> i) & 1U) != 0U)
                        gen.value[pinned_dims[i]] = tuple.value[pinned_dims[i]];
                events.push_back(GenEvent{.gen = gen, .mult = multiplicity, .src = tuple});
            }
        }
        std::ranges::sort(events, [](const GenEvent& lhs, const GenEvent& rhs) noexcept
                          { return cell_precedes(lhs.gen, rhs.gen); });

        PopulatedCube cube;
        cube.reserve(events.size());
        for (const GenEvent& event : events)
        {
            if (!cube.empty() && cube.back().cell == event.gen)
            {
                cube.back().agg.count += event.mult;
                cube.back().agg.closure = meet(cube.back().agg.closure, event.src);
            }
            else
                cube.push_back(
                    PopulatedCell{.cell = event.gen,
                                  .agg = CellAggregate{.count = event.mult, .closure = event.src}});
        }
        return cube;
    }

    // post: count(cell) from the cell_precedes-sorted cube, 0 when the cell is absent.
    [[nodiscard]] std::uint64_t count_in(const PopulatedCube& cube, const Cell& cell) noexcept
    {
        const auto found{std::ranges::lower_bound(cube, cell, cell_precedes, &PopulatedCell::cell)};
        return (found != cube.end() && found->cell == cell) ? found->agg.count : std::uint64_t{0};
    }

    // note: count(c) is recovered as the greatest count over the closed cells c generalizes.
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

    // invariant: a flat vector of distinct cells in cell_precedes order with summed counts.
    using BaseMultiset = std::vector<std::pair<Cell, std::uint64_t>>;

    [[nodiscard]] BaseMultiset sort_reduce_base(std::vector<std::pair<Cell, std::uint64_t>> events)
    {
        std::ranges::sort(events, [](const auto& lhs, const auto& rhs) noexcept
                          { return cell_precedes(lhs.first, rhs.first); });
        BaseMultiset base;
        base.reserve(events.size());
        for (const auto& [cell, count] : events)
        {
            if (!base.empty() && base.back().first == cell)
                base.back().second += count;
            else
                base.emplace_back(cell, count);
        }
        return base;
    }

    // post: the fully-pinned closed cells, plus one empty-WHERE residual per (level, role) equal to
    // count(L,*,R) minus the sum over its pinned components.
    [[nodiscard]] BaseMultiset recover_base(std::span<const InternalCell> closed)
    {
        std::vector<std::pair<Cell, std::uint64_t>> events;
        for (const InternalCell& entry : closed)
            if (entry.cell.pinned_count() == kNumDims)
                events.emplace_back(entry.cell, entry.count);

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
            const std::uint64_t total{count_in_closed(closed, star_where)};
            std::uint64_t pinned_sum{0};
            for (const InternalCell& entry : closed)
                if (entry.cell.pinned_count() == kNumDims &&
                    entry.cell.value[static_cast<std::size_t>(Dim::Level)] == level_id &&
                    entry.cell.value[static_cast<std::size_t>(Dim::Role)] == role_id)
                    pinned_sum += entry.count;
            if (total > pinned_sum)
                events.emplace_back(star_where, total - pinned_sum);
        }
        return sort_reduce_base(std::move(events));
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

    // post: the sorted distinct component leaves of the block's closed cells.
    // note: the fallback for a cube with no retained base, such as one parsed from the wire.
    [[nodiscard]] std::vector<std::string> labels_of(const CubeBlock& block)
    {
        std::set<std::string> uniq;
        for (const CubeCell& cell : block.cells)
            if (cell.coord.where && !cell.coord.where->empty())
                uniq.insert(cell.coord.where->back());
        return {uniq.begin(), uniq.end()};
    }

    // post: the sorted union of two component dictionaries.
    [[nodiscard]] std::vector<std::string> merge_dicts(std::span<const std::string> lhs,
                                                       std::span<const std::string> rhs)
    {
        std::set<std::string> uniq{lhs.begin(), lhs.end()};
        uniq.insert(rhs.begin(), rhs.end());
        return {uniq.begin(), uniq.end()};
    }

    // pre: `new_dict` contains `old_dict`.
    // post: only the WHERE component-id moves; kStar stays kStar and Level/Role ids are
    // dictionary-independent.
    [[nodiscard]] BaseMultiset remap_base(std::span<const std::pair<Cell, std::uint64_t>> base,
                                          std::span<const std::string> old_dict,
                                          std::span<const std::string> new_dict)
    {
        std::vector<std::pair<Cell, std::uint64_t>> events;
        events.reserve(base.size());
        for (const auto& [cell, count] : base)
        {
            Cell remapped{cell};
            const std::uint32_t where{cell.value[static_cast<std::size_t>(Dim::Where)]};
            if (where != kStar)
            {
                const auto found{std::ranges::lower_bound(new_dict, old_dict[where])};
                remapped.value[static_cast<std::size_t>(Dim::Where)] =
                    static_cast<std::uint32_t>(found - new_dict.begin());
            }
            events.emplace_back(remapped, count);
        }
        return sort_reduce_base(std::move(events));
    }

    // post: the domain base rows in canonical cell order; Where is a component-id or kStar.
    [[nodiscard]] std::vector<CubeBaseRow>
    to_base_rows(std::span<const std::pair<Cell, std::uint64_t>> base)
    {
        std::vector<CubeBaseRow> rows;
        rows.reserve(base.size());
        for (const auto& [cell, count] : base)
            rows.push_back(CubeBaseRow{
                .level = static_cast<LogLevel>(cell.value[static_cast<std::size_t>(Dim::Level)]),
                .component_id = cell.value[static_cast<std::size_t>(Dim::Where)],
                .role =
                    static_cast<StructuralRole>(cell.value[static_cast<std::size_t>(Dim::Role)]),
                .count = count});
        return rows;
    }

    // post: the interned base and dictionary land on the block with the closed cells.
    void retain_base(CubeBlock& block, std::span<const std::pair<Cell, std::uint64_t>> base,
                     std::span<const std::string> labels)
    {
        block.base = to_base_rows(base);
        block.base_component_dict.assign(labels.begin(), labels.end());
    }

    // post: the retained base when the block has one, else the lossless recovery from its closed
    // cells.
    [[nodiscard]] std::pair<BaseMultiset, std::vector<std::string>>
    interned_base_of(const CubeBlock& block)
    {
        if (!block.base.empty())
        {
            std::vector<std::pair<Cell, std::uint64_t>> events;
            events.reserve(block.base.size());
            for (const CubeBaseRow& row : block.base)
            {
                Cell cell;
                cell.value[static_cast<std::size_t>(Dim::Level)] =
                    static_cast<std::uint32_t>(row.level);
                cell.value[static_cast<std::size_t>(Dim::Where)] = row.component_id;
                cell.value[static_cast<std::size_t>(Dim::Role)] =
                    static_cast<std::uint32_t>(row.role);
                events.emplace_back(cell, row.count);
            }
            return {sort_reduce_base(std::move(events)), block.base_component_dict};
        }
        std::vector<std::string> labels{labels_of(block)};
        BaseMultiset base{recover_base(internal_cells(block, labels))};
        return {std::move(base), std::move(labels)};
    }

    // post: the closed cells in canonical coord order; raw_cell_count counts every populated cell
    // and cell_count the closed ones.
    // post: throws std::logic_error when the constructed WHERE chains are not a tree.
    [[nodiscard]] CubeBlock close_and_emit(std::span<const std::pair<Cell, std::uint64_t>> base,
                                           std::span<const std::string> labels)
    {
        const PopulatedCube cube{populate(base)};
        CubeBlock block;
        block.axes = reference_axes();
        block.raw_cell_count = cube.size();
        for (const auto& [cell, agg] : cube)
            if (agg.closure == cell)
                block.cells.push_back(
                    CubeCell{.coord = coord_of(cell, labels), .count = agg.count});
        block.cell_count = block.cells.size();

        const std::vector<std::vector<std::string>> paths{where_paths_of(block.cells)};
        if (!where_chain_is_tree(paths))
            throw std::logic_error{
                "metalog::cube: WHERE chain is not a single-parent tree (SPEC §16.5 MUST-1)"};
        retain_base(block, base, labels);
        return block;
    }

    // invariant: closure first, collapse last -- the budget bounds the whole cube, the trigger is
    // the closed-cell count, and the policy is the surjection plus its total-order tie-break.
    // refs: ADR-31.D8
    inline constexpr std::uint64_t kCubeCellBudget{4096};

    // invariant: level banding never crosses the Error/Fatal frontier, so those two are never
    // banded together.
    // note: Unknown sits above Fatal, so no floor reaches it -- it is not a severity.
    // refs: DN-43.D10
    inline constexpr std::uint32_t kMaxLevelBandFloor{static_cast<std::uint32_t>(LogLevel::Error)};
    // note: the WHERE tree is one depth-1 chain today, so truncation degenerates to a drop.
    inline constexpr std::uint32_t kFullWhereDepth{1};
    // note: dropping WHERE loses all location, so it is the last resort, costlier than a band.
    inline constexpr std::uint64_t kWhereDropCost{100};

    // invariant: level_band_floor 0 means no banding; f folds levels [0..f-1] into level f-1.
    // invariant: where_depth is the retained WHERE depth; below full is truncated, 0 dropped.
    struct CollapseState
    {
        std::uint32_t level_band_floor{0};
        std::uint32_t where_depth{kFullWhereDepth};
    };

    // post: a monotone surjection on one base cell -- LEVEL bands the bottom into its top
    // representative and a truncated WHERE drops to kStar.
    [[nodiscard]] Cell collapse_cell(Cell cell, const CollapseState& state) noexcept
    {
        if (cell.value[static_cast<std::size_t>(Dim::Level)] < state.level_band_floor)
            cell.value[static_cast<std::size_t>(Dim::Level)] = state.level_band_floor - 1;
        if (state.where_depth < kFullWhereDepth)
            cell.value[static_cast<std::size_t>(Dim::Where)] = kStar;
        return cell;
    }

    // post: the base coarsened by the state and re-reduced, since the surjection fuses cells.
    [[nodiscard]] BaseMultiset collapsed_base(const BaseMultiset& base, const CollapseState& state)
    {
        std::vector<std::pair<Cell, std::uint64_t>> events;
        events.reserve(base.size());
        for (const auto& [cell, count] : base)
            events.emplace_back(collapse_cell(cell, state), count);
        return sort_reduce_base(std::move(events));
    }

    // post: the reachable steps in the declared total order, LEVEL before WHERE.
    // note: admissibility is structural: LEVEL never crosses the frontier, WHERE only coarsens.
    [[nodiscard]] std::vector<CollapseState> next_collapse_steps(const CollapseState& state)
    {
        std::vector<CollapseState> steps;
        if (state.level_band_floor < kMaxLevelBandFloor)
        {
            CollapseState next{state};
            next.level_band_floor = (state.level_band_floor == 0) ? 2 : state.level_band_floor + 1;
            steps.push_back(next);
        }
        if (state.where_depth > 0)
        {
            CollapseState next{state};
            --next.where_depth;
            steps.push_back(next);
        }
        return steps;
    }

    // post: frozen and window-independent -- a LEVEL band costs its reach toward the frontier and a
    // WHERE drop the last-resort constant.
    [[nodiscard]] std::uint64_t collapse_step_cost(const CollapseState& source,
                                                   const CollapseState& target) noexcept
    {
        if (target.level_band_floor != source.level_band_floor)
            return target.level_band_floor;
        return kWhereDropCost;
    }

    // post: the admissible step maximizing delta cardinality over cost, or nullopt when no step
    // reduces cardinality.
    // assert: the strict > keeps the earlier candidate on a tie, so next_collapse_steps' fixed
    // order IS the declared tie-break.
    // refs: ADR-31.D8
    [[nodiscard]] std::optional<CollapseState>
    pick_collapse_step(const BaseMultiset& base, std::uint64_t current_cells,
                       const CollapseState& state, std::span<const std::string> labels)
    {
        std::optional<CollapseState> best;
        std::uint64_t best_delta{0};
        std::uint64_t best_cost{1};
        for (const CollapseState& candidate : next_collapse_steps(state))
        {
            const std::uint64_t cells{
                close_and_emit(collapsed_base(base, candidate), labels).cell_count};
            if (cells >= current_cells)
                continue;
            const std::uint64_t delta{current_cells - cells};
            const std::uint64_t cost{collapse_step_cost(state, candidate)};
            if (!best || delta * best_cost > best_delta * cost)
            {
                best = candidate;
                best_delta = delta;
                best_cost = cost;
            }
        }
        return best;
    }

    // post: the level axis carries band_floor and the where axis its floor_depth, so two cubes
    // compare only at equal collapse.
    void stamp_collapse(std::vector<CubeAxis>& axes, const CollapseState& state)
    {
        for (CubeAxis& axis : axes)
        {
            if (axis.name == "level" && state.level_band_floor > 0)
                axis.band_floor = state.level_band_floor;
            if (axis.name == "where")
                axis.floor_depth = state.where_depth;
        }
    }

    // post: the inverse of stamp_collapse; an absent band_floor means no banding and an absent
    // floor_depth the full chain.
    [[nodiscard]] CollapseState collapse_state_of(std::span<const CubeAxis> axes) noexcept
    {
        CollapseState state{};
        for (const CubeAxis& axis : axes)
        {
            if (axis.name == "level" && axis.band_floor)
                state.level_band_floor = *axis.band_floor;
            if (axis.name == "where" && axis.floor_depth)
                state.where_depth = *axis.floor_depth;
        }
        return state;
    }

    // post: the coarser state on each axis -- the finest granularity both cubes can be read at
    // without one manufacturing a distinction the other collapsed away.
    [[nodiscard]] CollapseState min_common_collapse(const CollapseState& lhs,
                                                    const CollapseState& rhs) noexcept
    {
        return {.level_band_floor = std::max(lhs.level_band_floor, rhs.level_band_floor),
                .where_depth = std::min(lhs.where_depth, rhs.where_depth)};
    }

    [[nodiscard]] std::vector<CubeAxis> collapsed_axes(const CollapseState& state)
    {
        std::vector<CubeAxis> axes{reference_axes()};
        stamp_collapse(axes, state);
        return axes;
    }

    // post: a flat categorical axis over the signed band vocabulary; a stored cube never carries
    // it, so compare-at-min never compares it.
    // note: kind is a value-SHAPE discriminator, so a string-valued ordinal axis is categorical.
    // refs: DN-42.D17
    [[nodiscard]] CubeAxis latency_shift_axis()
    {
        return CubeAxis{.name = "latency_shift",
                        .kind = "categorical",
                        .chain = std::nullopt,
                        .floor_depth = std::nullopt,
                        .band_floor = std::nullopt};
    }

    // post: closure first, then collapse-and-re-close while over budget; `initial` seeds the
    // starting depth.
    [[nodiscard]] CubeBlock build_bounded_cube(const BaseMultiset& base,
                                               std::span<const std::string> labels,
                                               CollapseState initial = {})
    {
        CollapseState state{initial};
        CubeBlock cube{close_and_emit(collapsed_base(base, state), labels)};
        while (cube.cell_count > kCubeCellBudget)
        {
            const std::optional<CollapseState> next{
                pick_collapse_step(base, cube.cell_count, state, labels)};
            if (!next)
                break;
            state = *next;
            cube = close_and_emit(collapsed_base(base, state), labels);
        }
        stamp_collapse(cube.axes, state);
        // assert: this is the only caller that runs the trigger, so the budget is declared here and
        // not in close_and_emit, whose trial closures are costed and discarded.
        cube.cell_budget = kCubeCellBudget;
        return cube;
    }

    // post: a cell is on the upper border iff it has no emergent parent, and on the lower border
    // iff it has no emergent child.
    [[nodiscard]] CubeBorder border_of(const PopulatedCube& anti, const PopulatedCube& mono,
                                       const PopulatedCube& previous, const PopulatedCube& current,
                                       std::span<const std::string> labels)
    {
        const auto emergent{
            [&](const Cell& cell) noexcept
            { return count_in(anti, cell) <= kThetaWas && count_in(mono, cell) >= kThetaNow; }};

        // assert: every emergent cell is a member of `mono`, which is cell_precedes-sorted, so the
        // parent lookup is a binary search.
        std::vector<Cell> cells;
        for (const PopulatedCell& populated : mono)
            if (emergent(populated.cell))
                cells.push_back(populated.cell);

        std::vector<bool> has_parent(cells.size(), false);
        std::vector<bool> has_child(cells.size(), false);
        for (std::size_t i{0}; i < cells.size(); ++i)
            for (std::size_t dim{0}; dim < kCellDims; ++dim)
            {
                if (!cells[i].pinned(static_cast<Dim>(dim)))
                    continue;
                Cell parent{cells[i]};
                parent.value[dim] = kStar;
                const auto found{std::ranges::lower_bound(cells, parent, cell_precedes)};
                if (found == cells.end() || *found != parent)
                    continue;
                has_parent[i] = true;
                has_child[static_cast<std::size_t>(found - cells.begin())] = true;
            }

        const auto border_cell{[&](const Cell& cell)
                               {
                                   return CubeBorderCell{.coord = coord_of(cell, labels),
                                                         .previous_count = count_in(previous, cell),
                                                         .current_count = count_in(current, cell)};
                               }};
        // assert: `cells` was built by iterating `mono`, so it is already in canonical order.
        CubeBorder out;
        // note: upper holds the minimal generators; lower the most specific cells.
        for (std::size_t i{0}; i < cells.size(); ++i)
        {
            if (!has_parent[i])
                out.upper.push_back(border_cell(cells[i]));
            if (!has_child[i])
                out.lower.push_back(border_cell(cells[i]));
        }
        return out;
    }

} // namespace

std::vector<CubeAxis> reference_axes()
{
    CubeAxis level{
        .name = "level", .kind = "categorical", .chain = std::nullopt, .floor_depth = std::nullopt};
    CubeAxis where{.name = "where",
                   .kind = "chain",
                   .chain = std::vector<std::string>{"component"},
                   .floor_depth = std::uint32_t{1}};
    CubeAxis role{.name = "structural_role",
                  .kind = "categorical",
                  .chain = std::nullopt,
                  .floor_depth = std::nullopt};
    return {std::move(level), std::move(where), std::move(role)};
}

bool where_chain_is_tree(std::span<const std::vector<std::string>> paths)
{
    // assert: a value under two parents is a DAG, which breaks prefix-truncation roll-up and leaves
    // the border ill-defined.
    // note: at depth 1 every component is a child of the root, so a flat chain is always a tree.
    std::map<std::pair<std::size_t, std::string>, std::string> parent_of;
    for (const std::vector<std::string>& path : paths)
        for (std::size_t depth{0}; depth < path.size(); ++depth)
        {
            const std::string parent{depth == 0 ? std::string{} : path[depth - 1]};
            const auto [iter,
                        inserted]{parent_of.try_emplace(std::pair{depth, path[depth]}, parent)};
            if (!inserted && iter->second != parent)
                return false;
        }
    return true;
}

CubeBlock build_closed_cube(std::span<const BaseRow> base_rows)
{
    // note: the window's dictionary is the sorted unique non-empty components of its base.
    std::set<std::string> uniq;
    for (const BaseRow& row : base_rows)
        if (!row.component.empty())
            uniq.emplace(row.component);
    const std::vector<std::string> labels{uniq.begin(), uniq.end()};

    std::vector<std::pair<Cell, std::uint64_t>> events;
    events.reserve(base_rows.size());
    for (const BaseRow& row : base_rows)
    {
        Cell cell;
        cell.value[static_cast<std::size_t>(Dim::Level)] = static_cast<std::uint32_t>(row.level);
        cell.value[static_cast<std::size_t>(Dim::Where)] = component_id(labels, row.component);
        cell.value[static_cast<std::size_t>(Dim::Role)] = static_cast<std::uint32_t>(row.role);
        events.emplace_back(cell, row.count);
    }
    return build_bounded_cube(sort_reduce_base(std::move(events)), labels);
}

CubeCoord cube_location(std::optional<LogLevel> level, std::string_view component)
// note: a LOCATION carries level and where only -- never role, never salience.
{
    CubeCoord coord;
    // assert: an engaged optional carrying Unknown is a location whose level was never observed; a
    // disengaged optional stars the axis by omission.
    // refs: DN-43.D10
    if (level)
        coord.level = level_to_spec_string(*level);
    if (!component.empty())
        coord.where = std::vector<std::string>{std::string{component}};
    return coord;
}

CubeDiffBlock
cube_diff_of(const CubeBlock& previous, const CubeBlock& current,
             const std::unordered_map<std::string, OrdinalDrift>& current_shift_by_component)
{
    // assert: two cubes at different collapse depths are read at the pair's minimal common depth,
    // projected into fresh coarse bases with neither stored cube mutated.
    const CollapseState common{
        min_common_collapse(collapse_state_of(previous.axes), collapse_state_of(current.axes))};
    const auto [prev_base, prev_dict]{interned_base_of(previous)};
    const auto [cur_base, cur_dict]{interned_base_of(current)};
    const std::vector<std::string> labels{merge_dicts(prev_dict, cur_dict)};

    // assert: the baseline projection is uniformly kStar, so a shifted component's WHERE-aggregate
    // stays balanced while its shift cell emerges.
    BaseMultiset prev_remapped{remap_base(collapsed_base(prev_base, common), prev_dict, labels)};
    BaseMultiset cur_remapped{remap_base(collapsed_base(cur_base, common), cur_dict, labels)};
    const bool has_shift{!current_shift_by_component.empty()};
    if (has_shift)
        for (auto& row : cur_remapped)
        {
            const std::uint32_t where_id{row.first.value[static_cast<std::size_t>(Dim::Where)]};
            if (where_id == kStar)
                continue;
            const auto found{current_shift_by_component.find(labels[where_id])};
            if (found != current_shift_by_component.end() &&
                found->second.shift != OrdinalShift::None)
                row.first.value[static_cast<std::size_t>(Dim::LatencyShift)] =
                    signed_shift_id(found->second.shift, found->second.direction);
        }
    const PopulatedCube cube_prev{populate(prev_remapped)};
    const PopulatedCube cube_cur{populate(cur_remapped)};

    CubeDiffBlock diff;
    diff.axes = collapsed_axes(common);
    if (has_shift)
        diff.axes.push_back(latency_shift_axis());
    // note: vanishing is the dual by the role swap; the shift pins on the current side only.
    CubeBorder emerging{border_of(cube_prev, cube_cur, cube_prev, cube_cur, labels)};
    CubeBorder vanishing{border_of(cube_cur, cube_prev, cube_prev, cube_cur, labels)};
    if (!emerging.lower.empty() || !emerging.upper.empty())
    {
        diff.emerging = std::move(emerging);
        diff.has_emerging = true;
    }
    if (!vanishing.lower.empty() || !vanishing.upper.empty())
    {
        diff.vanishing = std::move(vanishing);
        diff.has_vanishing = true;
    }
    return diff;
}

CubeBlock compose_cubes(const CubeBlock& lhs, const CubeBlock& rhs)
{
    // assert: the surjection distributes over the base sum, so sum-then-coarsen equals
    // coarsen-then-sum.
    // note: a composed cube is as precise as its coarsest member.
    const CollapseState common{
        min_common_collapse(collapse_state_of(lhs.axes), collapse_state_of(rhs.axes))};
    const auto [lhs_base, lhs_dict]{interned_base_of(lhs)};
    const auto [rhs_base, rhs_dict]{interned_base_of(rhs)};
    const std::vector<std::string> labels{merge_dicts(lhs_dict, rhs_dict)};
    const BaseMultiset lhs_remapped{remap_base(lhs_base, lhs_dict, labels)};
    const BaseMultiset rhs_remapped{remap_base(rhs_base, rhs_dict, labels)};
    std::vector<std::pair<Cell, std::uint64_t>> events;
    events.reserve(lhs_remapped.size() + rhs_remapped.size());
    events.insert(events.end(), lhs_remapped.begin(), lhs_remapped.end());
    events.insert(events.end(), rhs_remapped.begin(), rhs_remapped.end());
    return build_bounded_cube(sort_reduce_base(std::move(events)), labels, common);
}

} // namespace insight::metalog::cube
