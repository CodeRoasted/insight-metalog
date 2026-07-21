module;

module insight.metalog.detail.cube;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats; // level_to_spec_string

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

    // ── the diff-only signed latency_shift band (cube_differential_axes.md §4/§7.4) ──
    // The differential axis packs (magnitude × direction) into ONE Cell slot as a SIGNED,
    // polarity-MUTE value-id: NONE ≡ kStar (unpinned, the aggregated center); an UP band is its
    // magnitude (Low=1, Med=2, High=3), a DOWN band is magnitude + kMagnitudeBands (4, 5, 6). Up
    // and down are DISTINCT value-ids (⇒ distinct cells, both emerge against the all-NONE baseline)
    // but neither is judged good/bad here — the reading layer (eidos) maps up→regression,
    // down→recovery. The border treats the slot as a FLAT categorical dim (pin to a value-id, or
    // kStar-aggregate), never using value ORDER, so the order-convex (lower,upper) border stays
    // monotone by the §A4 proof exactly as for level/role; bidirectional only adds more categorical
    // values, it does not change the structure.
    inline constexpr std::uint32_t kMagnitudeBands{3}; // OrdinalShift Low/Med/High, per direction

    [[nodiscard]] std::uint32_t signed_shift_id(OrdinalShift shift,
                                                OrdinalDriftDirection direction) noexcept
    {
        const std::uint32_t magnitude{static_cast<std::uint32_t>(shift)}; // Low=1, Med=2, High=3
        return direction == OrdinalDriftDirection::Down ? magnitude + kMagnitudeBands : magnitude;
    }

    // Decode a pinned band id → its mute wire label ("up_low".."up_high" |
    // "down_low".."down_high"). The sign is oriented previous→current (the MetaLogDiff
    // previous/current stamp): up = current shifted higher than previous. NONE is never pinned (it
    // is the kStar baseline), so this is only ever called on a real band.
    [[nodiscard]] std::string signed_shift_label(std::uint32_t band_id)
    {
        const bool down{band_id > kMagnitudeBands};
        const auto magnitude{static_cast<OrdinalShift>(down ? band_id - kMagnitudeBands : band_id)};
        std::string label{down ? "down_" : "up_"};
        label.append(to_string(magnitude));
        return label;
    }

    // ── coord ↔ internal cell ───────────────────────────────────────────────────────

    // Internal Cell → wire coord, resolving the Where value-id back through the dictionary
    // as a depth-1 chain ([component]). An absent (starred) dim → an absent coord key (§16.4).
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
        // Diff-only differential axis (§4): a pinned LatencyShift slot renders its SIGNED band
        // ("up_high" / "down_low" …). Never pinned in a stored cube (SHIFT_NONE ≡ kStar is the
        // aggregated baseline), so a stored coord omits the key. The slot only ever holds a signed
        // band, never NONE.
        if (cell.pinned(Dim::LatencyShift))
            coord.latency_shift =
                signed_shift_label(cell.value[static_cast<std::size_t>(Dim::LatencyShift)]);
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
            cell.value[static_cast<std::size_t>(Dim::Where)] =
                component_id(labels, coord.where->back());
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
    [[nodiscard]] PopulatedCube populate(std::span<const std::pair<Cell, std::uint64_t>> base)
    {
        // Lever B (B1): emit every (gen_cell, mult, src_tuple) generation event into a flat vector,
        // SORT ONCE by cell_precedes, then linear-reduce adjacent equal cells — one bulk
        // cache-friendly sort instead of B·2ⁿ RB-tree inserts. The reduce is order-independent
        // (COUNT sums, `meet` is commutative+associative), so the unstable sort is bit-identical.
        // The sort IS the §16.4 order.
        struct GenEvent
        {
            Cell gen;
            std::uint64_t mult;
            Cell src;
        };
        std::vector<GenEvent> events;
        events.reserve(base.size() * (std::size_t{1} << kCellDims)); // upper bound: B·2ⁿ
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

    // Binary search the cell_precedes-sorted PopulatedCube (Lever B): count(cell), 0 if absent.
    [[nodiscard]] std::uint64_t count_in(const PopulatedCube& cube, const Cell& cell) noexcept
    {
        const auto found{
            std::lower_bound(cube.begin(), cube.end(), cell,
                             [](const PopulatedCell& populated, const Cell& target) noexcept
                             { return cell_precedes(populated.cell, target); })};
        return (found != cube.end() && found->cell == cell) ? found->agg.count : std::uint64_t{0};
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

    // ── the interned base as a flat multiset (Lever B, sibling of populate's flatten)
    // ─────────────── The base is accumulate-then-iterate only (no key lookups), so it is a flat
    // vector sorted by cell_precedes (distinct cells, summed counts) — never an RB-tree. Every
    // builder collects (cell, count) events and sort-reduces once. Order is irrelevant to the
    // emitted bytes (populate re-sorts) but kept canonical for the retained base + reproducibility.
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

    // Recover the base multiset: the fully-pinned closed cells are the non-empty-component
    // observations directly; each (level, role) carries an empty-component residual
    // = count((L,*,R)) − Σ_w count((L,w,R)), an empty-WHERE base tuple (Where starred).
    [[nodiscard]] BaseMultiset recover_base(std::span<const InternalCell> closed)
    {
        std::vector<std::pair<Cell, std::uint64_t>> events;
        for (const InternalCell& entry : closed)
            if (entry.cell.pinned_count() == kNumDims)
                events.emplace_back(entry.cell, entry.count);

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

    // Single-block WHERE dictionary = the sorted distinct component leaves of its closed cells. The
    // fallback dictionary when a cube carries no retained base (e.g. one parsed from the wire).
    [[nodiscard]] std::vector<std::string> labels_of(const CubeBlock& block)
    {
        std::set<std::string> uniq;
        for (const CubeCell& cell : block.cells)
            if (cell.coord.where && !cell.coord.where->empty())
                uniq.insert(cell.coord.where->back());
        return {uniq.begin(), uniq.end()};
    }

    // The merged WHERE dictionary of two cubes = the sorted union of their component dicts. Equal
    // to the old shared_labels (the union of both cubes' cell leaves), since a cube's base
    // dictionary IS its set of observed components == its closed-cell leaves — so the interned ids,
    // the canonical §16.4 order, and therefore the emitted bytes are identical to
    // recover-then-share.
    [[nodiscard]] std::vector<std::string> merge_dicts(std::span<const std::string> lhs,
                                                       std::span<const std::string> rhs)
    {
        std::set<std::string> uniq{lhs.begin(), lhs.end()};
        uniq.insert(rhs.begin(), rhs.end());
        return {uniq.begin(), uniq.end()};
    }

    // Re-intern a base from `old_dict` onto `new_dict` (new_dict ⊇ old_dict). Level/Role ids are
    // dictionary-independent; only the WHERE component-id moves (kStar stays kStar). Integer remap
    // by binary search — no string round-trip. Identity when the dicts coincide (the common case).
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
                const auto found{
                    std::lower_bound(new_dict.begin(), new_dict.end(), old_dict[where])};
                remapped.value[static_cast<std::size_t>(Dim::Where)] =
                    static_cast<std::uint32_t>(found - new_dict.begin());
            }
            events.emplace_back(remapped, count);
        }
        return sort_reduce_base(std::move(events));
    }

    // Internal interned base map → the DOMAIN CubeBaseRow vector retained on the block (canonical
    // CellLess order). Level/Role are pinned on every base cell; Where is the component-id or kStar
    // (== kStarComponent). The inverse of interned_base_of's fast path.
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

    // Retain the interned base + its dictionary on a freshly-built block (the §13 perf lever).
    // Called by close_and_emit so build_closed_cube AND compose_cubes retain atomically with the
    // closed cells.
    void retain_base(CubeBlock& block, std::span<const std::pair<Cell, std::uint64_t>> base,
                     std::span<const std::string> labels)
    {
        block.base = to_base_rows(base);
        block.base_component_dict.assign(labels.begin(), labels.end());
    }

    // The interned base + dictionary of a cube WITHOUT recover_base when it was retained (the hot
    // path — every in-memory cube). Fallback for a base-less cube (parsed from the wire /
    // hand-built): reconstruct from the closed cells via the lossless recover_base — its only
    // remaining caller.
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
                cell.value[static_cast<std::size_t>(Dim::Where)] =
                    row.component_id; // kStarComponent == kStar
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

    // Emit the closed cells of a populated cube as the wire block (canonical coord-sorted
    // order — the flat PopulatedCube is already cell_precedes-sorted, Lever B). raw_cell_count =
    // every populated cell; cell_count = the closed ones (closure == cell). MUST-1: the
    // constructed WHERE chains MUST be a single-parent tree.
    [[nodiscard]] CubeBlock close_and_emit(std::span<const std::pair<Cell, std::uint64_t>> base,
                                           std::span<const std::string> labels)
    {
        const PopulatedCube cube{populate(base)};
        CubeBlock block;
        block.axes = reference_axes();
        block.raw_cell_count = cube.size();
        for (const auto& [cell, agg] : cube)
            if (agg.closure == cell) // closed
                block.cells.push_back(
                    CubeCell{.coord = coord_of(cell, labels), .count = agg.count});
        block.cell_count = block.cells.size();

        const std::vector<std::vector<std::string>> paths{where_paths_of(block.cells)};
        if (!where_chain_is_tree(paths))
            throw std::logic_error{
                "metalog::cube: WHERE chain is not a single-parent tree (SPEC §16.5 MUST-1)"};
        retain_base(block, base, labels); // emit the interned base atomically with the closed cells
        return block;
    }

    // ── the per-window dimensional-collapse guardrail (cube_perf_and_collapse.md §C3) ────
    // An always-on cube can explode (O(B·2ⁿ), B ≤ ∏|dimᵢ|). The guardrail bounds EVERY window:
    // build the closed cube, and while it exceeds a static-constexpr budget, apply the best
    // admissible surjection (a monotone coarsening of the base ids) and RE-CLOSE, iterating until
    // under budget. Three separated objects: BUDGET (constexpr, whole-cube), TRIGGER (content read
    // — closed cells ≥ budget), POLICY (the surjection + the F5-M8 total-order tie-break, golden,
    // version-stamped). Closure-first, collapse-last: if closure alone fits, nothing degrades.

    // BUDGET — a static-constexpr bound on the whole cube (measure-first seeded 2026-07-04:
    // P_closed saturates ~4.5k at extreme cardinality, realistic windows sit far below → collapse
    // is a rare guard).
    inline constexpr std::uint64_t kCubeCellBudget{4096};

    // LEVEL banding never crosses the ERROR/FATAL severity frontier (canon order Trace<Debug<Info<
    // Warn<Error<Fatal): floor f ⇒ levels [0..f-1] merge into their top representative (f-1); the
    // max floor stops at Error's index so {Error,Fatal} are never banded (the distinction
    // failure_lexicon / role=Terminator / border-attribution all preserve). {Trace,Debug} is the
    // cheapest, near-lossless.
    inline constexpr std::uint32_t kMaxLevelBandFloor{
        static_cast<std::uint32_t>(LogLevel::Error)}; // 4
    // The WHERE tree is one depth-1 chain today ⇒ prefix-truncation degenerates to a drop (depth
    // 1→0); the framework generalizes to any 0 ≤ depth < full once the WHERE tree deepens (geo ×
    // code axes).
    inline constexpr std::uint32_t kFullWhereDepth{1};
    // WHERE-drop loses all location ⇒ structurally the last resort, far costlier than any LEVEL
    // band.
    inline constexpr std::uint64_t kWhereDropCost{100};

    // The per-window collapse depth — the version-stamped POLICY's output; a monotone surjection.
    struct CollapseState
    {
        std::uint32_t level_band_floor{0}; // 0 = none; f ⇒ levels [0..f-1] → level(f-1)
        std::uint32_t where_depth{
            kFullWhereDepth}; // retained WHERE depth; < full ⇒ truncated (0 = dropped)
    };

    // Apply the collapse state to one base cell's value-ids — the monotone surjection. LEVEL bands
    // the bottom into its top representative; WHERE (depth-1) drops to kStar when truncated. Pure
    // integer.
    [[nodiscard]] Cell collapse_cell(Cell cell, const CollapseState& state) noexcept
    {
        if (cell.value[static_cast<std::size_t>(Dim::Level)] < state.level_band_floor)
            cell.value[static_cast<std::size_t>(Dim::Level)] = state.level_band_floor - 1;
        if (state.where_depth < kFullWhereDepth)
            cell.value[static_cast<std::size_t>(Dim::Where)] = kStar;
        return cell;
    }

    // Coarsen the whole base by the collapse state, then sort-reduce (the surjection fuses base
    // cells).
    [[nodiscard]] BaseMultiset collapsed_base(const BaseMultiset& base, const CollapseState& state)
    {
        std::vector<std::pair<Cell, std::uint64_t>> events;
        events.reserve(base.size());
        for (const auto& [cell, count] : base)
            events.emplace_back(collapse_cell(cell, state), count);
        return sort_reduce_base(std::move(events));
    }

    // The reachable next collapse steps from `state`, in a FIXED order that IS the declared total
    // order (LEVEL before WHERE). Admissibility is a BARRIER, enforced by construction: LEVEL never
    // crosses the frontier; WHERE only toward the root.
    [[nodiscard]] std::vector<CollapseState> next_collapse_steps(const CollapseState& state)
    {
        std::vector<CollapseState> steps;
        if (state.level_band_floor < kMaxLevelBandFloor)
        {
            CollapseState next{state};
            next.level_band_floor =
                (state.level_band_floor == 0) ? 2 : state.level_band_floor + 1; // 0→2→3→4
            steps.push_back(next);
        }
        if (state.where_depth > 0)
        {
            CollapseState next{state};
            --next.where_depth; // depth-1 → 0 (drop); a deeper tree truncates one level toward the
                                // root
            steps.push_back(next);
        }
        return steps;
    }

    // Structural cost of a step (frozen, window-independent — anti-endogamy): a LEVEL band costs
    // its reach toward the frontier ({Trace,Debug} cheapest); WHERE-drop is the last-resort
    // constant.
    [[nodiscard]] std::uint64_t collapse_step_cost(const CollapseState& from,
                                                   const CollapseState& to) noexcept
    {
        if (to.level_band_floor != from.level_band_floor)
            return to.level_band_floor; // 2 < 3 < 4
        return kWhereDropCost;
    }

    // The version-stamped POLICY: pick the admissible step maximizing Δcardinality / cost. Δ is
    // measured by trial re-closure (collapse is rare; the candidate set is ≤2). Integer
    // cross-multiply, so no float; the STRICT `>` keeps the earlier candidate on a tie ⇒ the fixed
    // next_collapse_steps order (LEVEL before WHERE) IS the F5-M8 total-order tie-break. nullopt
    // when no step reduces cardinality.
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
                continue; // a collapse must pay in cardinality, else it is never chosen
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

    // Stamp the axes with the applied collapse so two cubes compare only at equal collapse (§C3):
    // the ORDINAL level axis carries band_floor, the WHERE chain axis its truncated floor_depth.
    void stamp_collapse(std::vector<CubeAxis>& axes, const CollapseState& state)
    {
        for (CubeAxis& axis : axes)
        {
            if (axis.name == "level" && state.level_band_floor > 0)
                axis.band_floor = state.level_band_floor;
            if (axis.name == "where")
                axis.floor_depth = state.where_depth; // full (1) uncollapsed; 0 when dropped
        }
    }

    // Read the collapse state back off a cube's axes (the inverse of stamp_collapse). Absent
    // band_floor ⇒ no LEVEL banding; the WHERE floor_depth defaults to the full depth-1 chain.
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

    // The MINIMAL COMMON collapse of a pair (§C3 compare-at-min): the COARSER on each axis — the
    // most banding (max level_band_floor) and the shallowest WHERE (min where_depth). Coarsening is
    // monotone, so this is the finest granularity BOTH cubes can be read at without one
    // manufacturing a distinction the other collapsed away (a cube that kept TRACE vs one that
    // folded it into DEBUG → read both at DEBUG). A no-op when the two already share a collapse
    // (the common case).
    [[nodiscard]] CollapseState min_common_collapse(const CollapseState& lhs,
                                                    const CollapseState& rhs) noexcept
    {
        return {std::max(lhs.level_band_floor, rhs.level_band_floor),
                std::min(lhs.where_depth, rhs.where_depth)};
    }

    // The reference axes stamped with a collapse depth (for a diff/compose result read at that
    // depth).
    [[nodiscard]] std::vector<CubeAxis> collapsed_axes(const CollapseState& state)
    {
        std::vector<CubeAxis> axes{reference_axes()};
        stamp_collapse(axes, state);
        return axes;
    }

    // The diff-only latency_shift differential-axis descriptor (cube_differential_axes.md §4): a
    // SIGNED ordinal band, NONE-centered (down_HIGH … NONE … up_HIGH) — polarity-MUTE (up/down is a
    // fact, the good/bad reading lives in eidos). A degenerate chain, so monotone-compatible with
    // the emerging border, and collapse-compatible via the UP_TO band semantic. EMERGENT-AT-DIFF:
    // no stored-cube domain (never in reference_axes → compare-at-min never compares it
    // diff-vs-state). Appended to a cube_diff's axes ONLY when a component shifted (either
    // direction); a stored cube never carries it.
    [[nodiscard]] CubeAxis latency_shift_axis()
    {
        return CubeAxis{.name = "latency_shift",
                        .kind = "ordinal",
                        .chain = std::nullopt,
                        .floor_depth = std::nullopt,
                        .band_floor = std::nullopt};
    }

    // Build the closed cube, then BOUND it: closure-first, collapse-if-over-budget, re-close,
    // iterate. `initial` seeds the collapse depth (default none for a raw window; compose seeds the
    // min common collapse so the merge is never finer than its coarsest member, §C3).
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
                break; // fully collapsed and still over budget — role×3-level-bands is tiny →
                       // unreachable
            state = *next;
            cube = close_and_emit(collapsed_base(base, state), labels);
        }
        stamp_collapse(cube.axes, state);
        return cube;
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
        const auto emergent{
            [&](const Cell& cell) noexcept
            { return count_in(anti, cell) <= kThetaWas && count_in(mono, cell) >= kThetaNow; }};

        // Every emergent cell has count(mono) ≥ θ_now ≥ 1, so it is a member of `mono`. `mono` is
        // cell_precedes-sorted (Lever B), so the emergent `cells` are too → parent lookup is a
        // binary search over them, no std::map index.
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
                parent.value[dim] = kStar; // un-pin one dim = one step toward the apex
                const auto found{
                    std::lower_bound(cells.begin(), cells.end(), parent, cell_precedes)};
                if (found == cells.end() || *found != parent)
                    continue; // parent not emergent
                has_parent[i] = true;
                has_child[static_cast<std::size_t>(found - cells.begin())] =
                    true; // the parent has an emergent child (cells[i])
            }

        const auto border_cell{[&](const Cell& cell)
                               {
                                   return CubeBorderCell{.coord = coord_of(cell, labels),
                                                         .previous_count = count_in(previous, cell),
                                                         .current_count = count_in(current, cell)};
                               }};
        // `cells` was populated by iterating `mono` (a cell_precedes-sorted vector), so it is
        // already in canonical cell order; upper/lower inherit that order — no re-sort needed.
        CubeBorder out;
        for (std::size_t i{0}; i < cells.size(); ++i)
        {
            if (!has_parent[i])
                out.upper.push_back(border_cell(cells[i])); // minimal generators = the headline
            if (!has_child[i])
                out.lower.push_back(
                    border_cell(cells[i])); // most-specific = the precise description
        }
        return out;
    }

} // namespace

std::vector<CubeAxis> reference_axes()
{
    // Canonical key order: Level, Where, Role (Dim order). WHERE is a depth-1 chain in
    // v0.6.0 (grounded in canon `component`); the chain + floor_depth carry the roll-up
    // mechanism so 1.5.5's dimensional-shrink is a content change, not a schema change.
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
            const auto [iter,
                        inserted]{parent_of.try_emplace(std::pair{depth, path[depth]}, parent)};
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

    std::vector<std::pair<Cell, std::uint64_t>> events;
    events.reserve(base_rows.size());
    for (const BaseRow& row : base_rows)
    {
        Cell cell;
        cell.value[static_cast<std::size_t>(Dim::Level)] =
            static_cast<std::uint32_t>(cube_level(row.level));
        cell.value[static_cast<std::size_t>(Dim::Where)] = component_id(labels, row.component);
        cell.value[static_cast<std::size_t>(Dim::Role)] = static_cast<std::uint32_t>(row.role);
        events.emplace_back(cell, row.count);
    }
    return build_bounded_cube(sort_reduce_base(std::move(events)), labels);
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

std::optional<CubeDiffBlock>
cube_diff_of(const CubeBlock& previous, const CubeBlock& current,
             const std::unordered_map<std::string, OrdinalDrift>& current_shift_by_component)
{
    // §C3 compare-at-min: two cubes at DIFFERENT collapse depths (one banded {TRACE,DEBUG}→DEBUG,
    // the other kept TRACE) cannot be compared at their native coords — read BOTH at the minimal
    // common collapse depth of the pair (the coarser on each axis). Each stored cube keeps its
    // native resolution for its other comparisons; this projects into FRESH coarse bases (no
    // mutation, no re-closure of the stored cube). At equal collapse (the common case) `common` is
    // a no-op and this is the direct diff. The "both documents carried a cube" presence-check is
    // the CALLER's (metalog::diff gates on has_cube).
    const CollapseState common{
        min_common_collapse(collapse_state_of(previous.axes), collapse_state_of(current.axes))};
    const auto [prev_base, prev_dict]{interned_base_of(previous)};
    const auto [cur_base, cur_dict]{interned_base_of(current)};
    const std::vector<std::string> labels{merge_dicts(prev_dict, cur_dict)};

    // The latency_shift differential axis (§4), diff-time only. The BASELINE projection is
    // uniformly SHIFT_NONE ≡ kStar, so prev is used as-is (its LatencyShift slot is never pinned).
    // The CURRENT projection pins LatencyShift to a SIGNED band for a component that shifted in
    // EITHER direction (≥LOW, up or down) — mapped by WHERE label through the shared dictionary.
    // NONE is never pinned (it IS the star baseline), so a shifted component's WHERE-aggregate cell
    // stays balanced across the diff (no spurious vanishing) while its (…, latency_shift=up_HIGH /
    // down_HIGH) cell EMERGES against the implicit all-NONE baseline. When the map is empty (no
    // comparable ordinal data, or nothing shifted) this is a no-op → the plain 3-D border.
    BaseMultiset prev_remapped{remap_base(collapsed_base(prev_base, common), prev_dict, labels)};
    BaseMultiset cur_remapped{remap_base(collapsed_base(cur_base, common), cur_dict, labels)};
    const bool has_shift{!current_shift_by_component.empty()};
    if (has_shift)
        for (auto& row : cur_remapped)
        {
            const std::uint32_t where_id{row.first.value[static_cast<std::size_t>(Dim::Where)]};
            if (where_id == kStar)
                continue; // no component → no shift attribution
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
    // emerging: appeared (anti = prev, monotone = cur). vanishing: the dual via the role
    // swap (anti = cur, monotone = prev) — same predicate, count_cur ≤ θ_was ∧ count_prev ≥ θ_now.
    // The shift only ever pins on the CURRENT side, so it participates in emergence only — a
    // shifted component's star-aggregate is balanced (vanishing sees the 3-D projection,
    // unchanged).
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

std::optional<CubeBlock> compose_cubes(const CubeBlock& lhs, const CubeBlock& rhs)
{
    // §12.1 + §C3 compose = MERGE: re-closed, not merged cell-by-cell, and rolled to the min common
    // collapse (a composed cube is as precise as its COARSEST member — a member banded to DEBUG
    // pulls the whole compose to DEBUG). Sum the two native bases; build_bounded_cube seeded at
    // `common` coarsens the merge to that depth, re-closes, and bounds it further if the merge
    // itself explodes. The surjection distributes over the base sum, so sum-then-coarsen ≡
    // coarsen-then-sum. The "both present, else omit (§16.7)" presence-check is the CALLER's
    // (metalog::compose).
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
