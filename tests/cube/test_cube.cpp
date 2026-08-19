// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
// The intra-window closed cube + its emerging-border `cube_diff`: closure / condensation, the
// order-convex (lower, upper) border, compose re-closure, the reservoir→cell LOCATION cross,
// the single-parent-tree guard on the WHERE chain, and a byte-identity golden.

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

// A CanonicalEvent carrying a template, level, component (the WHERE), and role. The
// component/template are string literals (static storage) → the string_views stay valid.
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

// Find a closed cell by its (optional) level + (optional) where-leaf + (optional) role.
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

// ── Block shape & condensation ──────────────────────────────────────────────────

TEST(CubeBlock, AlwaysBuiltEvenOnDefaultConfig)
{
    meta::MetaLogEngine engine; // 1.7.2: the cube is unconditional (no opt-in flag)
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    EXPECT_TRUE(doc.has_cube) << "the cube is always built (1.7.2 always-on)";
}

// ── Dimensional-collapse guardrail (the always-on cube's cardinality bound) ─────────────────────

// The oracle MUST exercise a collapse (ADR-31.D8 oracle-coverage): a cardinality-explosion window
// that actually FIRES the guardrail. 1500 distinct components, each at two bandable levels
// (Trace/Debug) → the un-collapsed cube exceeds the 4096-cell budget; the LEVEL interval-banding
// {Trace,Debug}→Debug fuses the pairs and brings it back under, WITHOUT dropping the WHERE axis.
TEST(CubeCollapse, GuardrailBoundsAnExplodingWindowByLevelBanding)
{
    static std::vector<std::string> comps; // static storage → the component string_views stay valid
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
    // The guardrail's CONTRACT: every window's cube is bounded by the budget.
    EXPECT_LE(doc.cube.cell_count, meta::CubeCardinalityStat::kCellsHard)
        << "collapse guardrail must bound the cube to the budget; got " << doc.cube.cell_count;
    // It must RECORD the applied collapse in the axes (so mismatched-collapse cubes are
    // detectable).
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
    // Determinism: the collapse is a pure function of content — a rebuild is bit-identical.
    EXPECT_EQ(doc.cube, build().cube) << "the collapse policy must be deterministic (ADR-31.D8)";
}

// Compare-at-min: two cubes at DIFFERENT collapse depths MUST still diff — a window that kept
// TRACE cannot be compared to one that banded {Trace,Debug}→Debug at native coords, so both are
// read at the minimal common depth (the coarser). Without it the diff would VANISH on axis mismatch
// — losing attribution at exactly the collapse transition. This proves it produces a diff at
// band_floor=2.
TEST(CubeCollapse, CompareAtMinDiffsAcrossDifferentCollapseDepths)
{
    const auto t0{std::chrono::system_clock::time_point{}};
    const auto t1{t0 + std::chrono::seconds{60}};

    // previous: a small window — NO collapse, keeps TRACE and DEBUG as distinct levels on "auth".
    meta::MetaLogEngine prev_engine{cube_cfg()};
    prev_engine.open_window(t0);
    for (int i = 0; i < 3; ++i)
    {
        prev_engine.ingest_event(ev("t", LogLevel::Trace, "auth"));
        prev_engine.ingest_event(ev("t", LogLevel::Debug, "auth"));
    }
    const auto prev{prev_engine.close_window(t1)};

    // current: the 1500-component explosion → LEVEL-banded {Trace,Debug}→Debug (band_floor=2).
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

    // The native axes DIFFER (prev: no band_floor; cur: band_floor=2). Compare-at-min reads BOTH at
    // the common band_floor=2 → the diff EXISTS (it would vanish under a bare axes-equality gate).
    const auto delta{meta::diff(prev, cur)};
    ASSERT_TRUE(delta.has_cube_diff)
        << "compare-at-min must diff across different collapse depths, not vanish on axis mismatch";
    std::optional<std::uint32_t> band;
    for (const auto& axis : delta.cube_diff.axes)
        if (axis.name == "level")
            band = axis.band_floor;
    EXPECT_EQ(band.value_or(0U), 2U)
        << "the diff is read at the minimal common depth (band_floor=2)";
    // Determinism: the compare-at-min projection is a pure function of content.
    EXPECT_EQ(meta::diff(prev, cur).cube_diff, delta.cube_diff)
        << "compare-at-min must be deterministic";
}

// Severity-safety (the critical guard): under budget pressure, LEVEL banding grows from the
// BOTTOM and NEVER crosses the ERROR/FATAL frontier. Here 2000 components each emit all six levels;
// even the maximal band (floor 4 = {Trace,Debug,Info,Warn}→Warn — the ceiling, kMaxLevelBandFloor=
// Error) leaves 3 distinct levels × 2000 comps ≫ 4096, so the guardrail MUST drop WHERE (depth 1→0)
// rather than band ERROR or FATAL. The frontier is sacred: {ERROR,FATAL} stay distinct at any
// pressure.
TEST(CubeCollapse, SeverityFrontierNeverCrossedWhereCollapsesInstead)
{
    static std::vector<std::string> comps; // static storage → the component string_views stay valid
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
    // LEVEL banding climbed to the frontier ceiling and stopped — it merged everything UP TO Warn
    // (floor 4) but never Error(4)/Fatal(5).
    ASSERT_TRUE(level_band.has_value()) << "LEVEL banding must have fired under this pressure";
    EXPECT_EQ(*level_band, 4U) << "band_floor must top out at the frontier boundary (Warn); it "
                                  "must NEVER reach FATAL — got "
                               << *level_band;
    // The frontier survives: ERROR and FATAL are DISTINCT cells, never fused, full mass each.
    const meta::CubeCell* err{find_cell(doc.cube, "ERROR", std::nullopt, "None")};
    const meta::CubeCell* fat{find_cell(doc.cube, "FATAL", std::nullopt, "None")};
    ASSERT_NE(err, nullptr) << "ERROR must survive collapse (frontier never banded)";
    ASSERT_NE(fat, nullptr) << "FATAL must survive collapse (frontier never banded)";
    EXPECT_EQ(err->count, 2000U) << "every ERROR event retained";
    EXPECT_EQ(fat->count, 2000U) << "every FATAL event retained";
    // Because LEVEL alone could not fit without crossing the frontier, a DIFFERENT axis collapsed:
    // WHERE dropped to the root (depth 0). This is the "collapse WHERE instead" clause, proven
    // live.
    EXPECT_EQ(where_depth.value_or(1U), 0U) << "WHERE must collapse when LEVEL banding maxes out "
                                               "below the frontier and still overflows";
    EXPECT_EQ(doc.cube, build().cube) << "the collapse policy must be deterministic (ADR-31.D8)";
}

// Closure-first / low-card stays full-depth: a window that fits after CLOSURE alone must NOT
// collapse — no LEVEL banding, full WHERE depth. Closure is lossless and applied always; collapse
// is lossy and applied only when over budget. A low-cardinality window degrades nothing.
TEST(CubeCollapse, ClosureFirstNoCollapseWhenUnderBudget)
{
    constexpr std::array<std::string_view, 10> kComps{
        "auth", "db", "cache", "web", "api", "ledger", "quota", "index", "replica", "manifest"};
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (const auto comp : kComps) // 10 comps × {Info, Error} = 20 base cells ≪ 4096 budget
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

// ── Cardinality monitor (the PURE compute; the eidos pipeline emits the WARN) ───────────────────

TEST(CubeCardinality, CountsDistinctPerAxisFromTheClosedCube)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    // 3 distinct components, 2 distinct levels (Info/Error), 1 role (None).
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
    // The pre-collapse WARN predicates were RETIRED; the meaningful
    // trigger is collapse_note(), which is empty on a small uncollapsed cube (nothing coarsened).
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

    // The reference axis set: level (categorical), where (chain, floor_depth 1), structural_role.
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

    // The window total lives in the closure of the fully-aggregated coord {}. Here all events share
    // role=None (level + where both vary), so the closed total-bearing cell is
    // {structural_role:"None"} (closure pins the constant role, stars the varying dims).
    const meta::CubeCell* apex{find_cell(c, std::nullopt, std::nullopt, "None")};
    ASSERT_NE(apex, nullptr) << "the window-total cell (closure of coord {}) must be present";
    EXPECT_EQ(apex->count, 8U) << "total == 5 + 3";

    // The joint cell (ERROR, db) must carry exactly the 3 timeouts.
    const meta::CubeCell* err_db{find_cell(c, "ERROR", "db", "None")};
    ASSERT_NE(err_db, nullptr);
    EXPECT_EQ(err_db->count, 3U);

    // Condensation: closure collapses redundant cells → cell_count ≤ raw_cell_count.
    EXPECT_GT(c.raw_cell_count, 0U);
    EXPECT_LE(c.cell_count, c.raw_cell_count);
    EXPECT_EQ(c.cell_count, c.cells.size());
}

TEST(CubeBlock, ClosureCollapsesSingleComponent)
{
    // Every event shares component=auth → (level, auth, role) and (level, *, role) carry
    // the SAME count, so the where-pinned cell is REDUNDANT (regenerates by closure) and is
    // NOT stored: closure collapse is real.
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int i = 0; i < 4; ++i)
        engine.ingest_event(ev("a", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);
    EXPECT_LT(doc.cube.cell_count, doc.cube.raw_cell_count)
        << "a single-component window must collapse (redundant where-pinned cells dropped)";
    // The fully-pinned base cell (INFO, auth, None) is always closed and present.
    EXPECT_NE(find_cell(doc.cube, "INFO", "auth", "None"), nullptr);
}

TEST(CubeBlock, EmptyComponentAggregatesNoWhere)
{
    // An event with empty component has NO where → it lands only in where-aggregated cells;
    // the apex still equals the full window total (empty-component events are counted).
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("x", LogLevel::Info, "")); // no component
    engine.ingest_event(ev("y", LogLevel::Info, "auth"));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);
    // Both events are INFO/None; the total-bearing closed cell is {level:INFO, role:None}
    // (where starred, since the empty-component event and the auth event differ on where).
    const meta::CubeCell* apex{find_cell(doc.cube, "INFO", std::nullopt, "None")};
    ASSERT_NE(apex, nullptr);
    EXPECT_EQ(apex->count, 2U)
        << "both events counted in the aggregate, incl. the empty-component one";
    // No where=auth cell may claim the empty-component event.
    const meta::CubeCell* info_auth{find_cell(doc.cube, "INFO", "auth", "None")};
    ASSERT_NE(info_auth, nullptr);
    EXPECT_EQ(info_auth->count, 1U);
}

// ── Emerging border ─────────────────────────────────────────────────────────────

namespace
{
// Two single-window documents under a shared processing contract, so diff() does not trip the
// comparability gate. Window B introduces an (ERROR, db) burst absent from window A.
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
        engine.ingest_event(ev("pool timeout", LogLevel::Error, "db")); // the burst
    const auto cur{engine.close_window(t2)};
    if (out_registry != nullptr)
        *out_registry =
            engine.registry(); // SRC-D-TIR-5: registry resolves template strings at serialise
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

    // The fully-specific cell (ERROR, db, None) is on the LOWER border (precise description).
    const meta::CubeBorderCell* lower{find_border(emerging.lower, "ERROR", "db", "None")};
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(lower->previous_count, 0U);
    EXPECT_EQ(lower->current_count, 5U);

    // The UPPER border (the headline = minimal generators) names the smallest condition
    // that characterises everything that emerged. Every upper cell appeared from nothing.
    ASSERT_FALSE(emerging.upper.empty());
    for (const auto& cell : emerging.upper)
    {
        EXPECT_EQ(cell.previous_count, 0U) << "an upper-border cell emerged from nothing (≤ θ_was)";
        EXPECT_GE(cell.current_count, 1U);
        // A minimal generator has no emergent parent: it is more general than the lower cell.
        EXPECT_LE(cell.coord.level.has_value() + (cell.coord.where.has_value()) +
                      cell.coord.structural_role.has_value(),
                  3);
    }
    // Nothing vanished (window A's auth traffic persists into B).
    EXPECT_FALSE(diff.cube_diff.has_vanishing);
}

TEST(CubeDiff, VanishingIsTheDual)
{
    // Swap the order: the burst is in the PREVIOUS window, gone from the current one.
    const auto [a, b]{two_windows()};
    const auto diff{meta::diff(b, a)}; // b has the burst, a does not → it vanishes

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

    // The cube is always built for a raw window; the only way a doc carries no cube is a
    // COMPOSED axis-mismatch clearing has_cube. Simulate that side here.
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

// ── Compose re-closure ──────────────────────────────────────────────────────────

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
    // Every event is (ERROR, db, None), so the whole cube collapses to one closed base
    // cell carrying the merged total — that single cell IS the apex (3 + 4 = 7).
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

    // A no-cube side now arises only from a prior composed axis-mismatch (has_cube
    // cleared); simulate it to exercise the "omit when either side lacks a cube" guard.
    meta::MetaLogEngine b{cfg};
    b.open_window(std::chrono::system_clock::time_point{});
    b.ingest_event(ev("t", LogLevel::Info, "auth"));
    auto without{b.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    without.has_cube = false;

    EXPECT_FALSE(meta::compose(with, without).has_cube)
        << "when either input omits a cube, the composed document must omit its cube too — "
           "composing a present cube with an absent one would under-count silently";
}

// ── Reservoir → cell LOCATION cross ─────────────────────────────────────────────

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
    engine.ingest_event(ev("disk failed", LogLevel::Fatal, "storage")); // rare-salient
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
    // Firewall: LOCATION-only — no salience, no role leaks into the cross.
    EXPECT_FALSE(fatal->cube_coord->structural_role.has_value());
}

// ── WHERE chain is a single-parent tree ─────────────────────────────────────────

TEST(CubeMustOne, TreeAcceptedDagRejected)
{
    // A proper tree: a/b and a/c share parent a — single-parent everywhere.
    const std::vector<std::vector<std::string>> tree{{"a", "b"}, {"a", "c"}, {"a"}};
    EXPECT_TRUE(cube::where_chain_is_tree(tree));

    // A DAG: node {x} appears under two different parents (a and b) → rejected.
    const std::vector<std::vector<std::string>> dag{{"a", "x"}, {"b", "x"}};
    EXPECT_FALSE(cube::where_chain_is_tree(dag));

    // Depth-1 chains (the depth-1 regime) are vacuously trees.
    const std::vector<std::vector<std::string>> flat{{"auth"}, {"db"}, {"web"}};
    EXPECT_TRUE(cube::where_chain_is_tree(flat));
}

// Note: the cube's cross-machine BYTE-IDENTITY proof is a cut/gate-time cross-leg assertion
// (.github/workflows/golden.yaml over the committed corpus), NOT an in-test frozen hash. The
// behavioral cube determinism (order-independence, collapse rebuild-equality) is covered below /
// in CubeCollapse.

// ── Order-independence (each cell's count is an order-independent integer sum) ────
// The cube's three dims are PER-LINE-PURE functions of the event (level / component /
// role), and each cell's COUNT is a plain sum — so the closed cube is invariant under ANY
// ingest permutation. Build a varied window forward and row-reversed; the closed cubes must
// be identical (per-cell coord+count, and cardinality). This is the single-component
// property the playground 25/27/28/29/30/31 `CubeDimsArePerLinePureAndOrderIndependent`
// proved through the full LogCraft replay — asserted here at the source, on the engine cube
// itself, which is why the playground copies could retire.
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

    // Canonical (level, where-leaf, role) → count extraction — order-independent by construction.
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

// ── The latency_shift differential axis ─────────────────────────────────────────────────
// The seam proof (Daidalos → Kleio): the Attribution Cube's first DIFFERENTIAL dimension is
// SIGNED + polarity-MUTE. A per-component latency shift EMERGES a cube_diff cell for EITHER
// direction (up = higher/slower, down = lower/faster); metalog judges NEITHER good/bad — the
// reading (up→regression, down→recovery) lives in eidos classify (already proven:
// insight-eidos/diff/tests/classify/ordinal_drift_test.cpp — MultiOctaveUp/DownIsHigh…). This
// suite owns the ENRICHMENT half: both directions enrich symmetrically, only the SIGN encodes
// previous→current orientation, and an unmoved latency emits nothing (no false emergence).
//
// Homing: metalog integration (engine → diff → cube_diff), NOT a LogCraft e2e — the axis is a
// pure diff-phase function of two windows' DurationLog2Ns histograms; the seam it needs is
// metalog↔eidos, and the "do(latency drift)" intervention is realized here at its surgical
// home (hold everything, move only payments' latency).
//
// Determinism: fixed seed-free integer ladder (floor(log2 ns), 48 bins); single worker; the
// (component → drift) map is a deterministic function of the inputs. No wall clock, no float→int.

namespace
{
constexpr std::int64_t kMsToNs{1'000'000}; // DurationLog2Ns schedule is nanoseconds
// 100 ms → 1e8 ns → bin 26; 100 s → 1e11 ns → bin 36. A 10-octave move ⇒ W1 HIGH bucket.
constexpr std::int64_t kLowLatencyMs{100};
constexpr std::int64_t kHighLatencyMs{100'000};
// Must clear the diff's thin-sample floor (ComponentOrdinal::kShiftSampleFloor = 32) so a real
// shift is ADMISSIBLE: below it the latency_shift axis is (correctly) projected to * as
// untrustworthy.
constexpr int kComponentCount{40};

// cube_cfg() + ordinal histograms enabled (the same-as-param batch gate) + a shared processing
// contract so diff() never trips the comparability gate.
[[nodiscard]] meta::MetaLogConfig latency_cfg()
{
    auto cfg{cube_cfg()};
    cfg.max_param_histograms = 2; // >0 enables ordinal (DurationLog2Ns) histograms
    cfg.canonicalization_version = "canon-latshift-test";
    cfg.retention_profile = "rp-latshift-test";
    return cfg;
}

// Ingest `count` events on `tmpl`/`level`/`component`, each carrying a latency_ms observation at
// `value_ms`. The ordinals span is rebuilt per event and valid through ingest (which copies).
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
        e.ordinals = obs; // span valid through this ingest call
        engine.ingest_event(e);
    }
}

// A prev/cur pair on ONE engine (shared registry, like two_windows). payments shifts from
// `prev_ms` → `cur_ms`; auth is a STABLE second component carrying no latency — it keeps
// payments' WHERE cell from collapsing as redundant (with a single component the closure stars the
// WHERE dimension away), so the shifted (…, where=payments, latency_shift) cell is a real pinned
// coord, not the aggregate.
// `event_count` lets the thin-floor test drop below kShiftSampleFloor; `emerge_in_current`
// gives the no-move control its ACTIVE structural change (a component that exists only in
// the current window), so its diff is non-empty without any latency move.
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

// Scan both border regions (emerging + vanishing, lower + upper) for a cell carrying a
// latency_shift coord at where-leaf `component`. The shift only ever pins on the CURRENT side,
// so a real match lands in emerging — vanishing is scanned so the control test can prove NONE.
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

// Any border cell carrying a latency_shift coord (regardless of component) — the control's
// "no false emergence anywhere" guard.
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

// Verbose-on-failure dump: every border cell's (region, level, where, latency_shift).
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

// "up_high" → "high" — the magnitude band, stripped of its direction sign.
[[nodiscard]] std::string magnitude_suffix(const std::string& band)
{
    const auto underscore{band.find('_')};
    return underscore == std::string::npos ? band : band.substr(underscore + 1);
}
} // namespace

// Drift arm — do(latency drift UP) at payments ⇒ an emerging cell (where=payments) carrying a
// SIGNED up_* band. eidos then reads this as a regression (proven eidos-side).
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

// Recovery arm + the polarity-MUTE regression proof — do(latency recovery / DOWN) at payments
// ⇒ an emerging cell carrying a SIGNED down_* band. This is the load-bearing assertion: the
// enrichment is polarity-MUTE, so the DOWN cell IS present. An up-clipped enrichment (the
// b60ec47 regression this proof guards) would drop it. eidos reads it as recovery (no alarm).
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

// Control arm — payments' latency does NOT move (a real, ACTIVE diff: a new 'cache' component
// emerges structurally), so NO shift axis and NO shift cell appear. Proves the axis is gated on
// latency movement, not on diff activity — no false emergence.
TEST(CubeDiffLatencyShift, NoLatencyMoveEmitsNoShiftAxisOrCell)
{
    // Same latency both sides; the emerging `cache` component is the ACTIVE structural change.
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

// Orientation + mute symmetry (the seam's metalog half) — swapping (previous, current) flips
// the SIGN up_* ↔ down_*, and BOTH directions carry the SAME magnitude band. The sign is the
// only thing that differs (it encodes previous→current); metalog judges neither — that IS the
// enrichment/reading seam: symmetric here, split downstream in eidos.
TEST(CubeDiffLatencyShift, SwapFlipsSignMuteSymmetry)
{
    const auto [low, high]{latency_shift_windows(kLowLatencyMs, kHighLatencyMs)};

    const auto up_diff{meta::diff(low, high)};   // previous=low, current=high ⇒ UP
    const auto down_diff{meta::diff(high, low)}; // swapped ⇒ DOWN

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

// Thin-sample floor — the DUAL of DriftUpEmergesUpShiftCell: the SAME real 10-octave
// move, but only 8 paired events (below ComponentOrdinal::kShiftSampleFloor = 32). ordinal_w1's
// thresholds are scale-relative, so without the floor 8-vs-8 would manufacture up_high; WITH it the
// axis is INADMISSIBLE and projected to * — no latency_shift axis, no shift cell. The declared
// error model: a thin window cannot carry the axis, so it says "unknown", never a manufactured
// verdict.
TEST(CubeDiffLatencyShift, ThinSampleProjectsShiftAxisToStar)
{
    constexpr int kThinCount{8}; // < kShiftSampleFloor (32)
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

// NOLINTEND
