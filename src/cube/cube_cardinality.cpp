module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;

namespace insight::metalog
{

// post: the distinct value count per axis plus the closed-cell count, read off the closed cube's
// coords, so it is correct for a cube that retained no base.
CubeCardinalityStat cube_cardinality(const CubeBlock& cube)
// note: a deterministic function of the closed cube; its values reach the acquisition block.
{
    CubeCardinalityStat stat;
    stat.cells = cube.cell_count;
    std::array<std::set<std::string_view>, CubeCardinalityStat::kAxisCount> distinct;
    for (const CubeCell& cell : cube.cells)
    {
        if (cell.coord.level)
            distinct[static_cast<std::size_t>(CardinalityAxis::Level)].insert(*cell.coord.level);
        if (cell.coord.where && !cell.coord.where->empty())
            distinct[static_cast<std::size_t>(CardinalityAxis::Component)].insert(
                cell.coord.where->back());
        if (cell.coord.structural_role)
            distinct[static_cast<std::size_t>(CardinalityAxis::Role)].insert(
                *cell.coord.structural_role);
    }
    for (std::size_t axis{0}; axis < CubeCardinalityStat::kAxisCount; ++axis)
        stat.per_axis[axis] = static_cast<std::uint32_t>(distinct[axis].size());
    return stat;
}

// post: nullopt when no axis was collapsed; otherwise one note per collapsed axis, joined with ';
// '.
std::optional<std::string> collapse_note(const CubeBlock& cube)
{
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
