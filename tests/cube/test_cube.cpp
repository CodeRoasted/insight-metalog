// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
// SPEC §16 intra-window closed cube + §13.6 emerging-border cube_diff: closure /
// condensation, the order-convex (lower, upper) border, compose re-closure, the §16.6
// reservoir→cell LOCATION cross, the §16.5 MUST-1 tree guard, and a byte-identity golden.

#include <gtest/gtest.h>
#include <picosha2.h>

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
[[nodiscard]] tok::CanonicalEvent
ev(std::string_view tmpl, LogLevel level, std::string_view component,
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

[[nodiscard]] const meta::CubeBorderCell* find_border(const std::vector<meta::CubeBorderCell>& cells,
                                                      std::optional<std::string> level,
                                                      std::optional<std::string> where_leaf,
                                                      std::optional<std::string> role)
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

// ── Block shape & condensation (§16.1/§16.2/§16.4) ──────────────────────────────

TEST(CubeBlock, AlwaysBuiltEvenOnDefaultConfig)
{
    meta::MetaLogEngine engine; // 1.7.2: the cube is unconditional (no opt-in flag)
    engine.open_window(std::chrono::system_clock::time_point{});
    engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    EXPECT_TRUE(doc.has_cube) << "the cube is always built (1.7.2 always-on)";
}

// ── §C3 dimensional-collapse guardrail (the always-on cube's cardinality bound) ─────────────────

// The oracle MUST exercise a collapse (F5-M8 oracle-coverage): a cardinality-explosion window
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
    // It must RECORD the applied collapse in the axes (so mismatched-collapse cubes are detectable).
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
    EXPECT_EQ(doc.cube, build().cube) << "the collapse policy must be deterministic (F5-M8)";
}

// ── §13 cardinality monitor (the PURE compute; the eidos pipeline emits the WARN) ───────────────

TEST(CubeCardinality, CountsDistinctPerAxisFromTheClosedCube)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    // 3 distinct components, 2 distinct levels (Info/Error), 1 role (None).
    engine.ingest_event(ev("a", LogLevel::Info, "auth"));
    engine.ingest_event(ev("b", LogLevel::Error, "db"));
    engine.ingest_event(ev("c", LogLevel::Info, "cache"));
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);

    const meta::CubeCardinalityStat card{meta::cube_cardinality(doc.cube)};
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Component)], 3U)
        << "distinct components auth/db/cache";
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Level)], 2U)
        << "distinct levels Info/Error";
    EXPECT_EQ(card.per_axis[static_cast<std::size_t>(meta::CardinalityAxis::Role)], 1U)
        << "single role None";
    EXPECT_EQ(card.cells, doc.cube.cell_count);
    EXPECT_FALSE(card.warns()) << "3 components ≪ warn threshold "
                               << meta::CubeCardinalityStat::kComponentWarn;
    EXPECT_EQ(card.offending_axis(), "none");
}

TEST(CubeCardinality, ThresholdVerdictsNameTheOffendingAxis)
{
    using Stat = meta::CubeCardinalityStat;
    const Stat ok{.cells = 10, .per_axis = {2, 5, 1}};
    EXPECT_FALSE(ok.warns());
    EXPECT_FALSE(ok.hard());
    EXPECT_EQ(ok.offending_axis(), "none");

    const Stat warn{.cells = 10, .per_axis = {2, Stat::kComponentWarn, 1}};
    EXPECT_TRUE(warn.warns());
    EXPECT_FALSE(warn.hard());
    EXPECT_EQ(warn.offending_axis(), "component") << "component is the unbounded WHERE axis";
    EXPECT_EQ(warn.offending_count(), Stat::kComponentWarn);

    const Stat hard_component{.cells = 10, .per_axis = {2, Stat::kComponentHard, 1}};
    EXPECT_TRUE(hard_component.hard());

    const Stat cells_breach{.cells = Stat::kCellsWarn, .per_axis = {2, 5, 1}};
    EXPECT_TRUE(cells_breach.warns());
    EXPECT_EQ(cells_breach.offending_axis(), "cells") << "cell count breaches with bounded axes";
}

TEST(CubeBlock, ReferenceAxesAndAggregateTotal)
{
    meta::MetaLogEngine engine{cube_cfg()};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(ev("login ok", LogLevel::Info, "auth"));
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(ev("timeout", LogLevel::Error, "db"));
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    ASSERT_TRUE(doc.has_cube);
    const meta::CubeBlock& c{doc.cube};

    // §16.2 reference axes: level (categorical), where (chain, floor_depth 1), structural_role.
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

    // §16.4: the window total lives in the closure of coord {}. Here all events share
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
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
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
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.has_cube);
    // Both events are INFO/None; the total-bearing closed cell is {level:INFO, role:None}
    // (where starred, since the empty-component event and the auth event differ on where).
    const meta::CubeCell* apex{find_cell(doc.cube, "INFO", std::nullopt, "None")};
    ASSERT_NE(apex, nullptr);
    EXPECT_EQ(apex->count, 2U) << "both events counted in the aggregate, incl. the empty-component one";
    // No where=auth cell may claim the empty-component event.
    const meta::CubeCell* info_auth{find_cell(doc.cube, "INFO", "auth", "None")};
    ASSERT_NE(info_auth, nullptr);
    EXPECT_EQ(info_auth->count, 1U);
}

// ── Emerging border (§13.6) ─────────────────────────────────────────────────────

namespace
{
// Two single-window documents under a shared contract, so diff() does not trip the
// §2.4 gate. Window B introduces an (ERROR, db) burst absent from window A.
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
        *out_registry = engine.registry(); // D-TIR-5: registry resolves template strings at serialise
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
    const meta::CubeBorderCell* lower{find_border(diff.cube_diff.vanishing.lower, "ERROR", "db", "None")};
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
    const auto doc_cube{with_cube.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    // The cube is always built for a raw window; the only way a doc carries no cube is a
    // COMPOSED axis-mismatch clearing has_cube (§16.7). Simulate that side here.
    meta::MetaLogEngine other{cfg};
    other.open_window(std::chrono::system_clock::time_point{});
    other.ingest_event(ev("a", LogLevel::Info, "auth"));
    auto doc_plain{other.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    doc_plain.has_cube = false;

    EXPECT_FALSE(meta::diff(doc_cube, doc_plain).has_cube_diff)
        << "§13.6: a cube_diff needs a cube on BOTH sides";
}

// ── Compose re-closure (§16.7 / §12.1) ──────────────────────────────────────────

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
    const auto with{a.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};

    // A no-cube side now arises only from a prior composed axis-mismatch (has_cube
    // cleared); simulate it to exercise the §16.7 "omit when either side lacks a cube" guard.
    meta::MetaLogEngine b{cfg};
    b.open_window(std::chrono::system_clock::time_point{});
    b.ingest_event(ev("t", LogLevel::Info, "auth"));
    auto without{b.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{1})};
    without.has_cube = false;

    EXPECT_FALSE(meta::compose(with, without).has_cube)
        << "§16.7: when either input omits a cube, the composed cube is omitted";
}

// ── §16.6 reservoir → cell LOCATION cross ───────────────────────────────────────

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
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};

    ASSERT_TRUE(doc.has_cube);
    const meta::ReservoirEntry* fatal{nullptr};
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_id == insight::template_id_of("disk failed"))
            fatal = &entry;
    ASSERT_NE(fatal, nullptr) << "the rare fatal must be in the reservoir";
    ASSERT_TRUE(fatal->cube_coord.has_value()) << "§16.6: a salient entry carries its cube LOCATION";
    EXPECT_EQ(fatal->cube_coord->level, "FATAL");
    ASSERT_TRUE(fatal->cube_coord->where.has_value());
    ASSERT_EQ(fatal->cube_coord->where->size(), 1U);
    EXPECT_EQ(fatal->cube_coord->where->front(), "storage");
    // Firewall (§16.6): LOCATION-only — no salience, no role leaks into the cross.
    EXPECT_FALSE(fatal->cube_coord->structural_role.has_value());
}

// ── §16.5 MUST-1 — WHERE chain is a single-parent tree ──────────────────────────

TEST(CubeMustOne, TreeAcceptedDagRejected)
{
    // A proper tree: a/b and a/c share parent a — single-parent everywhere.
    const std::vector<std::vector<std::string>> tree{
        {"a", "b"}, {"a", "c"}, {"a"}};
    EXPECT_TRUE(cube::where_chain_is_tree(tree));

    // A DAG: node {x} appears under two different parents (a and b) → rejected.
    const std::vector<std::vector<std::string>> dag{
        {"a", "x"}, {"b", "x"}};
    EXPECT_FALSE(cube::where_chain_is_tree(dag));

    // Depth-1 chains (the v0.6.0 regime) are vacuously trees.
    const std::vector<std::vector<std::string>> flat{{"auth"}, {"db"}, {"web"}};
    EXPECT_TRUE(cube::where_chain_is_tree(flat));
}

// ── Determinism golden (§16.9) ──────────────────────────────────────────────────
// A fixed cube-enabled two-window scenario, serialised (document + cube_diff). The
// SHA-256 is FROZEN: every stdlib / arch / OS MUST reproduce these exact bytes. A
// mismatch is a cube determinism regression. Re-derive ONLY for an intentional
// contract change (and re-verify across the cross-stdlib diagonal).
TEST(CubeDeterminism, ByteIdentityGolden)
{
    meta::TemplateRegistry registry;
    const auto [prev, cur]{two_windows(&registry)};
    const auto diff{meta::diff(prev, cur)};
    const std::string combined{meta::to_json(prev, registry) + "\n" + meta::to_json(cur, registry) +
                               "\n" + meta::to_json(diff)};
    const std::string digest{picosha2::hash256_hex_string(combined)};

    constexpr std::string_view kGolden{
        "9eb68b9c3a16643ead38581c8696507547880dad62ee802dc73066c9afb7352b"};
    EXPECT_EQ(digest, kGolden) << "cube wire bytes changed — re-derive across the cross-stdlib "
                                  "diagonal if intentional.\nactual combined:\n"
                               << combined;
}

// ── Order-independence (§16; the counts are an order-independent integer sum) ─────
// The cube's three dims are PER-LINE-PURE functions of the event (level / component /
// role), and each cell's COUNT is a plain sum — so the closed cube is invariant under ANY
// ingest permutation. Build a varied window forward and row-reversed; the closed cubes must
// be identical (per-cell coord+count, and cardinality). This is the single-component
// property the playground 25/27/28/29/30/31 `CubeDimsArePerLinePureAndOrderIndependent`
// proved through the full LogCraft replay — asserted here at the source, on the engine cube
// itself, so the playground copies retire (re-homing, ROADMAP §1.6.2).
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

// NOLINTEND
