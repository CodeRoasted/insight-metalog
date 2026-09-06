#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
namespace cube = insight::metalog::cube;
using insight::LogLevel;
using insight::StructuralRole;

// pre: `tmpl` and `component` outlive every use of the returned event -- it stores views.
[[nodiscard]] tok::CanonicalEvent ev(std::string_view tmpl, LogLevel level,
                                     std::string_view component,
                                     StructuralRole role = StructuralRole::None)
{
    tok::CanonicalEvent e;
    e.template_str = tmpl;
    e.level = level;
    e.component = component;
    e.structural_role = role;
    return e;
}

[[nodiscard]] meta::MetaLogConfig cube_cfg()
{
    return meta::MetaLogConfig{
        .reservoir_size = 8, .reservoir_per_kind_cap = 4, .emit_stability = false};
}

[[nodiscard]] const meta::CubeCell* find_cell(const meta::CubeBlock& c,
                                              std::optional<std::string> level,
                                              std::optional<std::string> where_leaf,
                                              std::optional<std::string> role)
{
    for (const auto& cell : c.cells)
    {
        const bool level_ok{cell.coord.level == level};
        const bool role_ok{cell.coord.structural_role == role};
        std::optional<std::string> cell_where;
        if (cell.coord.where && !cell.coord.where->empty())
            cell_where = cell.coord.where->back();
        const bool where_ok{cell_where == where_leaf};
        if (level_ok && role_ok && where_ok)
            return &cell;
    }
    return nullptr;
}

[[nodiscard]] const meta::CubeBorderCell*
find_border(const std::vector<meta::CubeBorderCell>& cells, std::optional<std::string> level,
            std::optional<std::string> where_leaf, std::optional<std::string> role)
{
    for (const auto& cell : cells)
    {
        std::optional<std::string> cw;
        if (cell.coord.where && !cell.coord.where->empty())
            cw = cell.coord.where->back();
        if (cell.coord.level == level && cell.coord.structural_role == role && cw == where_leaf)
            return &cell;
    }
    return nullptr;
}

} // namespace

TEST(CubeBlock, AlwaysBuiltEvenOnDefaultConfig)
{
    meta::MetaLogEngine engine;
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    EXPECT_TRUE(doc.has_cube) << "the cube is always built (1.7.2 always-on)";
}

// refs: ADR-31.D8, F-SRC-metalog-spec:SPEC.md
TEST(CubeCollapse, GuardrailBoundsAnExplodingWindowByLevelBanding)
{
    static std::vector<std::string> comps;
    if (comps.empty())
        for (int i = 0; i < 1500; ++i)
            comps.push_back("svc_" + std::to_string(i));
    const auto build{[&]
                     {
                         meta::MetaLogEngine engine{cube_cfg()};
                         engine.open_window(std::chrono::system_clock::time_point{});
                         for (const auto& comp : comps)
                         {
                             engine.ingest_event(ev("t", LogLevel::Trace, comp));
                             engine.ingest_event(ev("t", LogLevel::Debug, comp));
                         }
                         return engine.close_window(std::chrono::system_clock::time_point{} +
                                                    std::chrono::seconds{60});
                     }};
    const auto doc{build()};
    ASSERT_TRUE(doc.has_cube);
    EXPECT_LE(doc.cube.cell_count, meta::CubeCardinalityStat::kCellsHard)
        << "collapse guardrail must bound the cube to the budget; got " << doc.cube.cell_count;
    std::optional<std::uint32_t> level_band;
    std::optional<std::uint32_t> where_depth;
    for (const auto& axis : doc.cube.axes)
    {
        if (axis.name == "level")
            level_band = axis.band_floor;
        if (axis.name == "where")
            where_depth = axis.floor_depth;
    }
    ASSERT_TRUE(level_band.has_value())
        << "LEVEL banding must fire on this explosion (the free {Trace,Debug} move)";
    EXPECT_EQ(*level_band, 2U) << "{Trace,Debug} banded (floor 2); WHERE stays intact";
    EXPECT_EQ(where_depth.value_or(1U), 1U) << "LEVEL banding sufficed → WHERE not dropped";
    EXPECT_EQ(doc.cube, build().cube) << "the collapse policy must be deterministic (ADR-31.D8)";
}

// refs: DN-42.D17
TEST(CubeCollapse, CompareAtMinDiffsAcrossDifferentCollapseDepths)
{
    const auto t0{std::chrono::system_clock::time_point{}};
    const auto t1{t0 + std::chrono::seconds{60}};

    meta::MetaLogEngine prev_engine{cube_cfg()};
    prev_engine.open_window(t0);
    for (int i = 0; i < 3; ++i)
    {
        prev_engine.ingest_event(ev("t", LogLevel::Trace, "auth"));
        prev_engine.ingest_event(ev("t", LogLevel::Debug, "auth"));
    }
    const auto prev{prev_engine.close_window(t1)};

    static std::vector<std::string> comps;
    if (comps.empty())
        for (int i = 0; i < 1500; ++i)
            comps.push_back("svc_" + std::to_string(i));
    meta::MetaLogEngine cur_engine{cube_cfg()};
    cur_engine.open_window(t0);
    for (const auto& comp : comps)
    {
        cur_engine.ingest_event(ev("t", LogLevel::Trace, comp));
        cur_engine.ingest_event(ev("t", LogLevel::Debug, comp));
    }
    const auto cur{cur_engine.close_window(t1)};

    const auto delta{meta::diff(prev, cur)};
    ASSERT_TRUE(delta.has_cube_diff)
        << "compare-at-min must diff across different collapse depths, not vanish on axis mismatch";
    std::optional<std::uint32_t> band;
    for (const auto& axis : delta.cube_diff.axes)
        if (axis.name == "level")
            band = axis.band_floor;
    EXPECT_EQ(band.value_or(0U), 2U)
        << "the diff is read at the minimal common depth (band_floor=2)";
    EXPECT_EQ(meta::diff(prev, cur).cube_diff, delta.cube_diff)
        << "compare-at-min must be deterministic";
}

// refs: ADR-31.D8, F-SRC-metalog-spec:SPEC.md
TEST(CubeCollapse, SeverityFrontierNeverCrossedWhereCollapsesInstead)
{
    static std::vector<std::string> comps;
    if (comps.empty())
        for (int i = 0; i < 2000; ++i)
            comps.push_back("svc_" + std::to_string(i));
    const auto build{[&]
                     {
                         meta::MetaLogEngine engine{cube_cfg()};
                         engine.open_window(std::chrono::system_clock::time_point{});
                         for (const auto& comp : comps)
                         {
                             engine.ingest_event(ev("t", LogLevel::Trace, comp));
                             engine.ingest_event(ev("t", LogLevel::Debug, comp));
                             engine.ingest_event(ev("t", LogLevel::Info, comp));
                             engine.ingest_event(ev("t", LogLevel::Warn, comp));
                             engine.ingest_event(ev("t", LogLevel::Error, comp));
                             engine.ingest_event(ev("t", LogLevel::Fatal, comp));
                         }
                         return engine.close_window(std::chrono::system_clock::time_point{} +
                                                    std::chrono::seconds{60});
                     }};
    const auto doc{build()};
    ASSERT_TRUE(doc.has_cube);
    EXPECT_LE(doc.cube.cell_count, meta::CubeCardinalityStat::kCellsHard)
        << "the guardrail must still bound the cube; got " << doc.cube.cell_count;

    std::optional<std::uint32_t> level_band;
    std::optional<std::uint32_t> where_depth;
    for (const auto& axis : doc.cube.axes)
    {
        if (axis.name == "level")
            level_band = axis.band_floor;
        if (axis.name == "where")
            where_depth = axis.floor_depth;
    }
    ASSERT_TRUE(level_band.has_value()) << "LEVEL banding must have fired under this pressure";
    EXPECT_EQ(*level_band, 4U) << "band_floor must top out at the frontier boundary (Warn); it "
                                  "must NEVER reach FATAL — got "
                               << *level_band;
    const meta::CubeCell* err{find_cell(doc.cube, "ERROR", std::nullopt, "None")};
    const meta::CubeCell* fat{find_cell(doc.cube, "FATAL", std::nullopt, "None")};
    ASSERT_NE(err, nullptr) << "ERROR must survive collapse (frontier never banded)";
    ASSERT_NE(fat, nullptr) << "FATAL must survive collapse (frontier never banded)";
    EXPECT_EQ(err->count, 2000U) << "every ERROR event retained";
    EXPECT_EQ(fat->count, 2000U) << "every FATAL event retained";
    EXPECT_EQ(where_depth.value_or(1U), 0U) << "WHERE must collapse when LEVEL banding maxes out "
                                               "below the frontier and still overflows";
    EXPECT_EQ(doc.cube, build().cube) << "the collapse policy must be deterministic (ADR-31.D8)";
}

// refs: F-SRC-metalog-spec:SPEC.md
TEST(CubeCollapse, ClosureFirstNoCollapseWhenUnderBudget)
{
    constexpr std::array<std::string_view, 10> kComps{
        "auth", "db", "cache", "web", "api", "ledger", "quota", "index", "replica", "manifest"};
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (const auto comp : kComps)
    {
        engine.ingest_event(ev("t", LogLevel::Info, comp));
        engine.ingest_event(ev("t", LogLevel::Error, comp));
    }
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};
    ASSERT_TRUE(doc.has_cube);
    EXPECT_LT(doc.cube.cell_count, meta::CubeCardinalityStat::kCellsHard)
        << "the low-card cube must fit after closure alone; got " << doc.cube.cell_count;

    std::optional<std::uint32_t> level_band;
    std::optional<std::uint32_t> where_depth;
    for (const auto& axis : doc.cube.axes)
    {
        if (axis.name == "level")
            level_band = axis.band_floor;
        if (axis.name == "where")
            where_depth = axis.floor_depth;
    }
    EXPECT_FALSE(level_band.has_value())
        << "a fitting window must NOT band LEVEL (closure-first — nothing degraded)";
    EXPECT_EQ(where_depth.value_or(1U), 1U)
        << "a fitting window must keep FULL WHERE depth (no truncation)";
}

TEST(CubeCardinality, CountsDistinctPerAxisFromTheClosedCube)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("a", LogLevel::Info, "auth"));
    engine.ingest_event(ev("b", LogLevel::Error, "db"));
    engine.ingest_event(ev("c", LogLevel::Info, "cache"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);

    const meta::CubeCardinalityStat card{meta::cube_cardinality(doc.cube)};
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Component)], 3U)
        << "distinct components auth/db/cache";
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Level)], 2U)
        << "distinct levels Info/Error";
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Role)], 1U)
        << "single role None";
    EXPECT_EQ(card.cells, doc.cube.cell_count);
    EXPECT_FALSE(meta::collapse_note(doc.cube).has_value()) << "a 3-component cube is uncollapsed";
    EXPECT_LE(card.cells, meta::CubeCardinalityStat::kCellsHard) << "the cube stays under budget";
}

TEST(CubeBlock, ReferenceAxesAndAggregateTotal)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(ev("timeout", LogLevel::Error, "db"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    ASSERT_TRUE(doc.has_cube);
    const meta::CubeBlock& c{doc.cube};

    ASSERT_EQ(c.axes.size(), 3U);
    EXPECT_EQ(c.axes[0].name, "level");
    EXPECT_EQ(c.axes[0].kind, "categorical");
    EXPECT_EQ(c.axes[1].name, "where");
    EXPECT_EQ(c.axes[1].kind, "chain");
    ASSERT_TRUE(c.axes[1].chain.has_value());
    ASSERT_EQ(c.axes[1].chain->size(), 1U);
    EXPECT_EQ(c.axes[1].chain->front(), "component");
    ASSERT_TRUE(c.axes[1].floor_depth.has_value());
    EXPECT_EQ(*c.axes[1].floor_depth, 1U);
    EXPECT_EQ(c.axes[2].name, "structural_role");

    const meta::CubeCell* apex{find_cell(c, std::nullopt, std::nullopt, "None")};
    ASSERT_NE(apex, nullptr) << "the window-total cell (closure of coord {}) must be present";
    EXPECT_EQ(apex->count, 8U) << "total == 5 + 3";

    const meta::CubeCell* err_db{find_cell(c, "ERROR", "db", "None")};
    ASSERT_NE(err_db, nullptr);
    EXPECT_EQ(err_db->count, 3U);

    EXPECT_GT(c.raw_cell_count, 0U);
    EXPECT_LE(c.cell_count, c.raw_cell_count);
    EXPECT_EQ(c.cell_count, c.cells.size());
}

TEST(CubeBlock, ClosureCollapsesSingleComponent)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int i = 0; i < 4; ++i)
        engine.ingest_event(ev("a", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);
    EXPECT_LT(doc.cube.cell_count, doc.cube.raw_cell_count)
        << "a single-component window must collapse (redundant where-pinned cells dropped)";
    EXPECT_NE(find_cell(doc.cube, "INFO", "auth", "None"), nullptr);
}

TEST(CubeBlock, EmptyComponentAggregatesNoWhere)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("x", LogLevel::Info, ""));
    engine.ingest_event(ev("y", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);
    const meta::CubeCell* apex{find_cell(doc.cube, "INFO", std::nullopt, "None")};
    ASSERT_NE(apex, nullptr);
    EXPECT_EQ(apex->count, 2U)
        << "both events counted in the aggregate, incl. the empty-component one";
    const meta::CubeCell* info_auth{find_cell(doc.cube, "INFO", "auth", "None")};
    ASSERT_NE(info_auth, nullptr);
    EXPECT_EQ(info_auth->count, 1U);
}

namespace
{
// post: two single-window documents sharing one processing contract; the second carries an
// (ERROR, db) burst the first has not.
[[nodiscard]] std::pair<meta::MetaLogDocument, meta::MetaLogDocument>
two_windows(meta::TemplateRegistry* out_registry = nullptr)
{
    auto cfg{cube_cfg()};
    cfg.canonicalization_version = "canon-cube-test";
    cfg.retention_profile = "rp-cube-test";

    meta::MetaLogEngine engine{cfg};
    const std::chrono::system_clock::time_point t0{};
    const auto t1{t0 + std::chrono::seconds{60}};
    const auto t2{t0 + std::chrono::seconds{120}};

    engine.open_window(t0);
    for (int i = 0; i < 6; ++i)
        engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    const auto prev{engine.close_window(t1)};

    engine.open_window(t1);
    for (int i = 0; i < 6; ++i)
        engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(ev("pool timeout", LogLevel::Error, "db"));
    const auto cur{engine.close_window(t2)};
    // refs: SRC-D-TIR-5
    if (out_registry != nullptr)
        *out_registry = engine.registry();
    return {prev, cur};
}
} // namespace

TEST(CubeDiff, EmergingHeadlineIsMinimalGenerator)
{
    const auto [prev, cur]{two_windows()};
    const auto diff{meta::diff(prev, cur)};

    ASSERT_TRUE(diff.has_cube_diff) << "both docs carried a cube with equal axes";
    EXPECT_EQ(diff.cube_diff.axes.size(), 3U);
    ASSERT_TRUE(diff.cube_diff.has_emerging) << "the (ERROR, db) burst must emerge";
    const meta::CubeBorder& emerging{diff.cube_diff.emerging};

    const meta::CubeBorderCell* lower{find_border(emerging.lower, "ERROR", "db", "None")};
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(lower->previous_count, 0U);
    EXPECT_EQ(lower->current_count, 5U);

    ASSERT_FALSE(emerging.upper.empty());
    for (const auto& cell : emerging.upper)
    {
        EXPECT_EQ(cell.previous_count, 0U) << "an upper-border cell emerged from nothing (≤ θ_was)";
        EXPECT_GE(cell.current_count, 1U);
        EXPECT_LE(cell.coord.level.has_value() + (cell.coord.where.has_value()) +
                      cell.coord.structural_role.has_value(),
                  3);
    }
    EXPECT_FALSE(diff.cube_diff.has_vanishing);
}

TEST(CubeDiff, VanishingIsTheDual)
{
    const auto [a, b]{two_windows()};
    const auto diff{meta::diff(b, a)};

    ASSERT_TRUE(diff.has_cube_diff);
    ASSERT_TRUE(diff.cube_diff.has_vanishing);
    const meta::CubeBorderCell* lower{
        find_border(diff.cube_diff.vanishing.lower, "ERROR", "db", "None")};
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(lower->previous_count, 5U);
    EXPECT_EQ(lower->current_count, 0U);
    EXPECT_FALSE(diff.cube_diff.has_emerging);
}

TEST(CubeDiff, OmittedWhenOneSideHasNoCube)
{
    auto cfg{cube_cfg()};
    cfg.canonicalization_version = "c";
    meta::MetaLogEngine with_cube{cfg};
    with_cube.open_window(std::chrono::system_clock::time_point{});
    with_cube.ingest_event(ev("a", LogLevel::Info, "auth"));
    const auto doc_cube{
        with_cube.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    meta::MetaLogEngine other{cfg};
    other.open_window(std::chrono::system_clock::time_point{});
    other.ingest_event(ev("a", LogLevel::Info, "auth"));
    auto doc_plain{
        other.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    doc_plain.has_cube = false;

    EXPECT_FALSE(meta::diff(doc_cube, doc_plain).has_cube_diff)
        << "a cube_diff requires a cube on BOTH sides: one side without a cube leaves the "
           "comparison undefined, so no cube_diff may be produced";
}

TEST(CubeCompose, RecloseSumsCounts)
{
    auto cfg{cube_cfg()};
    cfg.canonicalization_version = "c";
    cfg.retention_profile = "r";

    const auto build{[&](LogLevel level, std::string_view comp, int n)
                     {
                         meta::MetaLogEngine engine{cfg};
                         engine.open_window(std::chrono::system_clock::time_point{});
                         for (int i = 0; i < n; ++i)
                             engine.ingest_event(ev("t", level, comp));
                         return engine.close_window(std::chrono::system_clock::time_point{} +
                                                    std::chrono::seconds{1});
                     }};
    const auto lhs{build(LogLevel::Error, "db", 3)};
    const auto rhs{build(LogLevel::Error, "db", 4)};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_TRUE(composed.has_cube) << "both inputs had a cube → re-closed cube emitted";
    const meta::CubeCell* err_db{find_cell(composed.cube, "ERROR", "db", "None")};
    ASSERT_NE(err_db, nullptr);
    EXPECT_EQ(err_db->count, 7U) << "distributive counts add (3 + 4) under re-closure";
}

TEST(CubeCompose, OmittedWhenOneSideHasNoCube)
{
    auto cfg{cube_cfg()};
    cfg.canonicalization_version = "c";
    cfg.retention_profile = "r";
    meta::MetaLogEngine a{cfg};
    a.open_window(std::chrono::system_clock::time_point{});
    a.ingest_event(ev("t", LogLevel::Info, "auth"));
    const auto with{
        a.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    meta::MetaLogEngine b{cfg};
    b.open_window(std::chrono::system_clock::time_point{});
    b.ingest_event(ev("t", LogLevel::Info, "auth"));
    auto without{b.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    without.has_cube = false;

    EXPECT_FALSE(meta::compose(with, without).has_cube)
        << "when either input omits a cube, the composed document must omit its cube too — "
           "composing a present cube with an absent one would under-count silently";
}

TEST(CubeReservoirCross, SalientEntryCarriesLocation)
{
    meta::MetaLogConfig cfg{.top_k_size = 2, .reservoir_size = 8, .emit_stability = false};
    meta::MetaLogEngine engine{cfg};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int i = 0; i < 50; ++i)
    {
        engine.ingest_event(ev("steady a", LogLevel::Info, "web"));
        engine.ingest_event(ev("steady b", LogLevel::Info, "web"));
        engine.ingest_event(ev("steady c", LogLevel::Info, "web"));
    }
    engine.ingest_event(ev("disk failed", LogLevel::Fatal, "storage"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};

    ASSERT_TRUE(doc.has_cube);
    const meta::ReservoirEntry* fatal{nullptr};
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_id == insight::template_id_of("disk failed"))
            fatal = &entry;
    ASSERT_NE(fatal, nullptr) << "the rare fatal must be in the reservoir";
    ASSERT_TRUE(fatal->cube_coord.has_value())
        << "a salient reservoir entry must carry its cube LOCATION (level + WHERE chain) so a "
           "consumer can place the evidence in the cube without re-deriving it";
    EXPECT_EQ(fatal->cube_coord->level, "FATAL");
    ASSERT_TRUE(fatal->cube_coord->where.has_value());
    ASSERT_EQ(fatal->cube_coord->where->size(), 1U);
    EXPECT_EQ(fatal->cube_coord->where->front(), "storage");
    EXPECT_FALSE(fatal->cube_coord->structural_role.has_value());
}

TEST(CubeMustOne, TreeAcceptedDagRejected)
{
    const std::vector<std::vector<std::string>> tree{{"a", "b"}, {"a", "c"}, {"a"}};
    EXPECT_TRUE(cube::where_chain_is_tree(tree));

    const std::vector<std::vector<std::string>> dag{{"a", "x"}, {"b", "x"}};
    EXPECT_FALSE(cube::where_chain_is_tree(dag));

    const std::vector<std::vector<std::string>> flat{{"auth"}, {"db"}, {"web"}};
    EXPECT_TRUE(cube::where_chain_is_tree(flat));
}

// note: the cube's cross-machine byte-identity proof is the golden workflow, not an in-test hash.
// refs: F-SRC-insight-metalog:golden.yaml
TEST(CubeDeterminism, OrderIndependentUnderRowReversal)
{
    const std::vector<tok::CanonicalEvent> events{
        ev("login ok", LogLevel::Info, "auth"),
        ev("pool timeout", LogLevel::Error, "db"),
        ev("cache miss", LogLevel::Warn, "cache"),
        ev("login ok", LogLevel::Info, "auth"),
        ev("request done", LogLevel::Info, "api", StructuralRole::GroupEnd),
        ev("pool timeout", LogLevel::Error, "db"),
        ev("batch begin", LogLevel::Info, "api", StructuralRole::GroupBegin),
    };

    const auto build_cube = [](const std::vector<tok::CanonicalEvent>& evs)
    {
        meta::MetaLogEngine engine{cube_cfg()};
        const std::chrono::system_clock::time_point t0{};
        engine.open_window(t0);
        for (const auto& e : evs)
            engine.ingest_event(e);
        return engine.close_window(t0 + std::chrono::seconds{1});
    };

    const auto cells_of = [](const meta::CubeBlock& cube)
    {
        std::map<std::tuple<std::string, std::string, std::string>, std::uint64_t> by_coord;
        for (const auto& cell : cube.cells)
        {
            const std::string where_leaf{cell.coord.where && !cell.coord.where->empty()
                                             ? cell.coord.where->back()
                                             : std::string{"*"}};
            by_coord[{cell.coord.level.value_or("*"), where_leaf,
                      cell.coord.structural_role.value_or("*")}] += cell.count;
        }
        return by_coord;
    };

    const auto forward{build_cube(events)};
    std::vector<tok::CanonicalEvent> reversed{events};
    std::reverse(reversed.begin(), reversed.end());
    const auto backward{build_cube(reversed)};

    ASSERT_TRUE(forward.has_cube);
    ASSERT_TRUE(backward.has_cube);
    EXPECT_EQ(forward.cube.cell_count, backward.cube.cell_count)
        << "closed cell count differs under row reversal";
    EXPECT_EQ(cells_of(forward.cube), cells_of(backward.cube))
        << "the closed cube must be identical under row reversal (per-line-pure dims, "
           "order-independent counts)";
}

namespace
{
constexpr std::int64_t kMsToNs{1'000'000};
constexpr std::int64_t kLowLatencyMs{100};
constexpr std::int64_t kHighLatencyMs{100'000};
constexpr int kComponentCount{40};

[[nodiscard]] meta::MetaLogConfig latency_cfg()
{
    auto cfg{cube_cfg()};
    cfg.max_param_histograms = 2;
    cfg.canonicalization_version = "canon-latshift-test";
    cfg.retention_profile = "rp-latshift-test";
    return cfg;
}

void ingest_latency(meta::MetaLogEngine& engine, std::string_view tmpl, LogLevel level,
                    std::string_view component, std::int64_t value_ms, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const std::array<insight::OrdinalObservation, 1> obs{
            {{.field_name = "latency_ms",
              .schedule = insight::OrdinalSchedule::DurationLog2Ns,
              .value = value_ms * kMsToNs}}};
        auto e{ev(tmpl, level, component)};
        e.ordinals = obs;
        engine.ingest_event(e);
    }
}

[[nodiscard]] std::pair<meta::MetaLogDocument, meta::MetaLogDocument>
latency_shift_windows(std::int64_t prev_ms, std::int64_t cur_ms, int event_count = kComponentCount,
                      bool emerge_in_current = false)
{
    meta::MetaLogEngine engine{latency_cfg()};
    const std::chrono::system_clock::time_point t0{};
    const auto t1{t0 + std::chrono::seconds{60}};
    const auto t2{t0 + std::chrono::seconds{120}};

    engine.open_window(t0);
    ingest_latency(engine, "charge card <*>", LogLevel::Info, "payments", prev_ms, event_count);
    for (int i = 0; i < event_count; ++i)
        engine.ingest_event(ev("auth ok", LogLevel::Info, "auth"));
    const auto prev{engine.close_window(t1)};

    engine.open_window(t1);
    ingest_latency(engine, "charge card <*>", LogLevel::Info, "payments", cur_ms, event_count);
    for (int i = 0; i < event_count; ++i)
        engine.ingest_event(ev("auth ok", LogLevel::Info, "auth"));
    if (emerge_in_current)
        for (int i = 0; i < 5; ++i)
            engine.ingest_event(ev("cache warm", LogLevel::Info, "cache"));
    const auto cur{engine.close_window(t2)};
    return {prev, cur};
}

[[nodiscard]] std::string border_where_leaf(const meta::CubeBorderCell& cell)
{
    if (cell.coord.where && !cell.coord.where->empty())
        return cell.coord.where->back();
    return "*";
}

[[nodiscard]] bool has_latency_shift_axis(const meta::CubeDiffBlock& diff)
{
    for (const auto& axis : diff.axes)
        if (axis.name == "latency_shift")
            return true;
    return false;
}

[[nodiscard]] const meta::CubeBorderCell* find_shift_cell(const meta::CubeDiffBlock& diff,
                                                          std::string_view component)
{
    const auto scan{[&](const meta::CubeBorder& border) -> const meta::CubeBorderCell*
                    {
                        for (const auto* region : {&border.lower, &border.upper})
                            for (const auto& cell : *region)
                                if (cell.coord.latency_shift &&
                                    border_where_leaf(cell) == component)
                                    return &cell;
                        return nullptr;
                    }};
    if (diff.has_emerging)
        if (const auto* hit{scan(diff.emerging)})
            return hit;
    if (diff.has_vanishing)
        if (const auto* hit{scan(diff.vanishing)})
            return hit;
    return nullptr;
}

[[nodiscard]] bool any_shift_cell(const meta::CubeDiffBlock& diff)
{
    const auto in{[](const meta::CubeBorder& border)
                  {
                      for (const auto* region : {&border.lower, &border.upper})
                          for (const auto& cell : *region)
                              if (cell.coord.latency_shift)
                                  return true;
                      return false;
                  }};
    return (diff.has_emerging && in(diff.emerging)) || (diff.has_vanishing && in(diff.vanishing));
}

[[nodiscard]] std::string shift_dump(const meta::CubeDiffBlock& diff)
{
    std::string out{"cube_diff cells [region · level · where · latency_shift]:\n"};
    const auto emit{[&](std::string_view region, const meta::CubeBorder& border)
                    {
                        for (const auto* part : {&border.lower, &border.upper})
                            for (const auto& cell : *part)
                            {
                                out += "  ";
                                out += region;
                                out += " · ";
                                out += cell.coord.level.value_or("*");
                                out += " · ";
                                out += border_where_leaf(cell);
                                out += " · ";
                                out += cell.coord.latency_shift.value_or("<none>");
                                out += '\n';
                            }
                    }};
    if (diff.has_emerging)
        emit("emerging", diff.emerging);
    if (diff.has_vanishing)
        emit("vanishing", diff.vanishing);
    if (!diff.has_emerging && !diff.has_vanishing)
        out += "  <no border>\n";
    return out;
}

[[nodiscard]] std::string magnitude_suffix(const std::string& band)
{
    const auto underscore{band.find('_')};
    return underscore == std::string::npos ? band : band.substr(underscore + 1);
}
} // namespace

// refs: F-SRC-insight-eidos:ordinal_drift_test.cpp
TEST(CubeDiffLatencyShift, DriftUpEmergesUpShiftCell)
{
    const auto [prev, cur]{latency_shift_windows(kLowLatencyMs, kHighLatencyMs)};
    const auto diff{meta::diff(prev, cur)};

    ASSERT_TRUE(diff.has_cube_diff) << "a latency shift on payments must produce a cube_diff";
    EXPECT_TRUE(has_latency_shift_axis(diff.cube_diff))
        << "the latency_shift axis must be declared when a component shifted\n"
        << shift_dump(diff.cube_diff);
    const meta::CubeBorderCell* cell{find_shift_cell(diff.cube_diff, "payments")};
    ASSERT_NE(cell, nullptr) << "expected an emerging (where=payments, latency_shift=up_*) cell\n"
                             << shift_dump(diff.cube_diff);
    ASSERT_TRUE(cell->coord.latency_shift.has_value());
    EXPECT_EQ(*cell->coord.latency_shift, "up_high")
        << "10-octave UP shift ⇒ up_high; got " << *cell->coord.latency_shift;
}

TEST(CubeDiffLatencyShift, RecoveryDownEmergesDownShiftCell)
{
    const auto [prev, cur]{latency_shift_windows(kHighLatencyMs, kLowLatencyMs)};
    const auto diff{meta::diff(prev, cur)};

    ASSERT_TRUE(diff.has_cube_diff) << "a DOWN latency shift must ALSO produce a cube_diff";
    const meta::CubeBorderCell* cell{find_shift_cell(diff.cube_diff, "payments")};
    ASSERT_NE(cell, nullptr)
        << "REGRESSION GUARD: the DOWN cell must be present (polarity-MUTE, not up-clipped)\n"
        << shift_dump(diff.cube_diff);
    ASSERT_TRUE(cell->coord.latency_shift.has_value());
    EXPECT_EQ(*cell->coord.latency_shift, "down_high")
        << "10-octave DOWN shift ⇒ down_high; got " << *cell->coord.latency_shift;
}

TEST(CubeDiffLatencyShift, NoLatencyMoveEmitsNoShiftAxisOrCell)
{
    const auto [prev, cur]{latency_shift_windows(kLowLatencyMs, kLowLatencyMs, kComponentCount,
                                                 /*emerge_in_current=*/true)};

    const auto diff{meta::diff(prev, cur)};
    ASSERT_TRUE(diff.has_cube_diff) << "the 'cache' emergence must produce an (active) cube_diff";
    EXPECT_FALSE(has_latency_shift_axis(diff.cube_diff))
        << "no component shifted ⇒ the latency_shift axis must NOT be declared\n"
        << shift_dump(diff.cube_diff);
    EXPECT_EQ(find_shift_cell(diff.cube_diff, "payments"), nullptr)
        << "payments' latency did not move — it must carry no shift cell\n"
        << shift_dump(diff.cube_diff);
    EXPECT_FALSE(any_shift_cell(diff.cube_diff))
        << "no cell may carry a latency_shift under an unmoved-latency diff\n"
        << shift_dump(diff.cube_diff);
}

TEST(CubeDiffLatencyShift, SwapFlipsSignMuteSymmetry)
{
    const auto [low, high]{latency_shift_windows(kLowLatencyMs, kHighLatencyMs)};

    const auto up_diff{meta::diff(low, high)};
    const auto down_diff{meta::diff(high, low)};

    const meta::CubeBorderCell* up_cell{find_shift_cell(up_diff.cube_diff, "payments")};
    const meta::CubeBorderCell* down_cell{find_shift_cell(down_diff.cube_diff, "payments")};
    ASSERT_NE(up_cell, nullptr) << shift_dump(up_diff.cube_diff);
    ASSERT_NE(down_cell, nullptr) << shift_dump(down_diff.cube_diff);
    ASSERT_TRUE(up_cell->coord.latency_shift.has_value());
    ASSERT_TRUE(down_cell->coord.latency_shift.has_value());

    const std::string up_band{*up_cell->coord.latency_shift};
    const std::string down_band{*down_cell->coord.latency_shift};
    EXPECT_TRUE(up_band.starts_with("up_")) << up_band;
    EXPECT_TRUE(down_band.starts_with("down_")) << down_band;
    EXPECT_EQ(magnitude_suffix(up_band), magnitude_suffix(down_band))
        << "polarity-MUTE: the two directions must carry the SAME magnitude band — only the "
           "sign differs. up="
        << up_band << " down=" << down_band;
}

TEST(CubeDiffLatencyShift, ThinSampleProjectsShiftAxisToStar)
{
    constexpr int kThinCount{8};
    const auto [prev, cur]{latency_shift_windows(kLowLatencyMs, kHighLatencyMs, kThinCount)};

    const auto diff{meta::diff(prev, cur)};
    EXPECT_FALSE(has_latency_shift_axis(diff.cube_diff))
        << "a thin (<32-event) pairing must not declare the latency_shift axis — even though the "
           "underlying latency really moved 10 octaves, the sample is too thin to trust\n"
        << shift_dump(diff.cube_diff);
    EXPECT_EQ(find_shift_cell(diff.cube_diff, "payments"), nullptr)
        << "no shift cell may be manufactured from a below-floor sample\n"
        << shift_dump(diff.cube_diff);
}
