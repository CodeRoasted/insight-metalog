module;

module insight.metalog;
import insight.metalog.internal; // std
import insight.metalog.api;      // CubeBlock / CubeCardinalityStat / CubeAxis / CubeCell

// §13 cardinality monitor — the PURE compute (cube_perf_and_collapse.md C2). Distinct value count
// per axis (level/component/role) + the closed-cell count, read from the closed cube's coords. Read
// from the coords, not the retained base, so it is correct for ANY cube (incl. a wire-parsed one
// with no base). OBSERVABILITY ONLY: a deterministic function of the counts that never feeds the
// deterministic content stream — the eidos pipeline emits the gated WARN naming the offending axis.
// metalog excludes spdlog by design, so the compute lives here, the log fires where logging does.

namespace insight::metalog
{

CubeCardinalityStat cube_cardinality(const CubeBlock& cube)
{
    CubeCardinalityStat stat;
    stat.cells = cube.cell_count;
    std::array<std::set<std::string_view>, CubeCardinalityStat::kAxisCount> distinct;
    for (const CubeCell& cell : cube.cells)
    {
        if (cell.coord.level)
            distinct[static_cast<std::size_t>(CardinalityAxis::Level)].insert(*cell.coord.level);
        if (cell.coord.where && !cell.coord.where->empty())
            distinct[static_cast<std::size_t>(CardinalityAxis::Component)].insert(cell.coord.where->back());
        if (cell.coord.structural_role)
            distinct[static_cast<std::size_t>(CardinalityAxis::Role)].insert(*cell.coord.structural_role);
    }
    for (std::size_t axis{0}; axis < CubeCardinalityStat::kAxisCount; ++axis)
        stat.per_axis[axis] = static_cast<std::uint32_t>(distinct[axis].size());
    return stat;
}

std::optional<std::string> collapse_note(const CubeBlock& cube)
{
    // A collapse was applied iff an axis carries a band_floor > 0 (level interval-banding, §C3) or a
    // floor_depth below its full chain length (WHERE-tree prefix-truncation). stamp_collapse writes
    // the applied state onto the axes; an uncollapsed axis carries band_floor 0/absent and
    // floor_depth == full chain length, so neither branch fires.
    std::string note;
    const auto add{[&note](const std::string& part)
                   {
                       if (!note.empty())
                           note += "; ";
                       note += part;
                   }};
    for (const CubeAxis& axis : cube.axes)
    {
        if (axis.band_floor && *axis.band_floor > 0)
            add(axis.name + " banded to floor " + std::to_string(*axis.band_floor));
        if (axis.floor_depth && axis.chain && *axis.floor_depth < axis.chain->size())
            add(axis.name + " truncated to depth " + std::to_string(*axis.floor_depth) + "/" +
                std::to_string(axis.chain->size()));
    }
    if (note.empty())
        return std::nullopt;
    return note;
}

} // namespace insight::metalog
