#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import insight.metalog.test;
import insight.semantic.github;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

constexpr double kRecallFloor{0.95};
constexpr double kMispointCeiling{0.05};

// invariant: a dimension left empty is a wildcard -- the antichain is declared by dimension.
struct DeclaredCell
{
    std::string level;
    std::vector<std::string> where;
    std::string role;
};

[[nodiscard]] meta::CubeCoord coord_of(const DeclaredCell& cell)
{
    meta::CubeCoord coord;
    if (!cell.level.empty())
        coord.level = cell.level;
    if (!cell.where.empty())
        coord.where = cell.where;
    if (!cell.role.empty())
        coord.structural_role = cell.role;
    return coord;
}

[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path)
{
    std::ifstream input{path};
    if (!input)
        throw std::runtime_error("cube border: fixture not found: " + path.string());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
        if (!line.empty())
            lines.push_back(line);
    if (lines.empty())
        throw std::runtime_error("cube border: empty fixture: " + path.string());
    return lines;
}

// pre: `composed` outlives this call.
[[nodiscard]] meta::MetaLogDocument build_doc(const std::vector<std::string>& lines,
                                              const insight::semantic::ComposedSemantics& composed)
{
    meta::MetaLogConfig config;
    meta::MetaLogEngine engine{config};
    const auto start{std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}};
    engine.open_window(start);

    tok::ArenaAllocator arena{std::size_t{1} << 22};
    tok::Tokenizer tokenizer{arena, tok::MaskConfig{}, composed};
    for (const auto& line : lines)
    {
        const auto event{tokenizer.process_line(line)};
        if (!event)
            throw std::runtime_error("cube border: line failed to tokenize: " + line);
        engine.ingest_event(*event);
    }
    return engine.close_window(start + std::chrono::seconds{60});
}

[[nodiscard]] std::vector<meta::CubeCoord>
produced_upper(const std::filesystem::path& dir,
               const insight::semantic::ComposedSemantics& composed)
{
    const auto baseline{build_doc(read_lines(dir / "baseline.jsonl"), composed)};
    const auto changed{build_doc(read_lines(dir / "changed.jsonl"), composed)};
    const auto report{meta::diff(baseline, changed)};
    if (!report.has_cube_diff || !report.cube_diff.has_emerging)
        return {};
    std::vector<meta::CubeCoord> upper;
    for (const auto& cell : report.cube_diff.emerging.upper)
        upper.push_back(cell.coord);
    return upper;
}

struct Score
{
    double recall{0.0};
    double mispoint{0.0};
    std::size_t declared_count{0};
    std::size_t produced_count{0};
};

[[nodiscard]] Score score_border(const std::vector<DeclaredCell>& declared,
                                 const std::vector<meta::CubeCoord>& produced)
{
    const auto contains{[&produced](const meta::CubeCoord& wanted)
                        {
                            return std::ranges::any_of(produced,
                                                       [&wanted](const meta::CubeCoord& got)
                                                       { return got == wanted; });
                        }};
    std::size_t recovered{0};
    for (const auto& cell : declared)
        if (contains(coord_of(cell)))
            ++recovered;

    std::size_t stray{0};
    for (const auto& got : produced)
    {
        const bool declared_here{std::ranges::any_of(declared, [&got](const DeclaredCell& cell)
                                                     { return coord_of(cell) == got; })};
        if (!declared_here)
            ++stray;
    }

    Score score;
    score.declared_count = declared.size();
    score.produced_count = produced.size();
    score.recall = declared.empty()
                       ? 1.0
                       : static_cast<double>(recovered) / static_cast<double>(declared.size());
    score.mispoint =
        produced.empty() ? 0.0 : static_cast<double>(stray) / static_cast<double>(produced.size());
    return score;
}

[[nodiscard]] std::string render(const std::vector<meta::CubeCoord>& coords)
{
    std::string out;
    for (const auto& coord : coords)
    {
        out += "\n      (level=" + coord.level.value_or("*");
        out += ", where=";
        if (coord.where)
            for (const auto& leaf : *coord.where)
                out += leaf + "/";
        else
            out += "*";
        out += ", role=" + coord.structural_role.value_or("*") + ")";
    }
    return out.empty() ? " <none>" : out;
}

struct Class
{
    std::string difficulty;
    std::string directory;
    std::vector<DeclaredCell> declared_upper;
};

[[nodiscard]] std::vector<Class> classes()
{
    std::vector<DeclaredCell> diffuse;
    for (int index{1}; index <= 16; ++index)
    {
        std::string name{"svc"};
        name += (index < 10 ? "0" : "");
        name += std::to_string(index);
        diffuse.push_back(DeclaredCell{.where = {name}});
    }
    return {
        Class{.difficulty = "single-generator",
              .directory = "single_generator",
              .declared_upper = {DeclaredCell{.where = {"cache"}}}},
        Class{
            .difficulty = "multi-generator-antichain",
            .directory = "multi_generator",
            .declared_upper = {DeclaredCell{.level = "FATAL"}, DeclaredCell{.role = "Terminator"}}},
        Class{.difficulty = "diffuse-near-cap",
              .directory = "diffuse_near_cap",
              .declared_upper = diffuse},
        Class{
            .difficulty = "ambiguous-equal",
            .directory = "ambiguous_equal",
            .declared_upper = {DeclaredCell{.where = {"blue"}}, DeclaredCell{.where = {"green"}}}},
    };
}

} // namespace

TEST(CubeEmergingBorder, RecoversDeclaredAntichainPerDifficultyClass)
{
    const std::array manifests{insight::semantic::github::kManifest};
    const auto composed{insight::semantic::compose(manifests)};
    const std::filesystem::path root{std::filesystem::path{INSIGHT_METALOG_FIXTURE_DIR} /
                                     "cube_emerging_border"};

    for (const auto& cls : classes())
    {
        const auto produced{produced_upper(root / cls.directory, composed)};
        const auto score{score_border(cls.declared_upper, produced)};

        EXPECT_GE(score.recall, kRecallFloor)
            << "[" << cls.difficulty << "] emerging-border recall " << score.recall << " < floor "
            << kRecallFloor << " (declared " << score.declared_count << ", produced "
            << score.produced_count << ")\n  produced:" << render(produced);
        EXPECT_LE(score.mispoint, kMispointCeiling)
            << "[" << cls.difficulty << "] mis-pointing " << score.mispoint << " > ceiling "
            << kMispointCeiling << " (declared " << score.declared_count << ", produced "
            << score.produced_count << ")\n  produced:" << render(produced);
    }
}
