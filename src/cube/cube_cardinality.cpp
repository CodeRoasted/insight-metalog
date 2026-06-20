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

} // namespace insight::metalog
