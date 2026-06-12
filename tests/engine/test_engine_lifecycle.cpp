// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// MetaLogEngine lifecycle + window envelope: open/close guards, reset semantics,
// duration/lines_observed.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

TEST(MetaLogEngineLifecycle, ResetBetweenWindowsKeepsStabilityState)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("a"));
    auto doc1{engine.close_window(t0 + std::chrono::seconds(1))};
    EXPECT_EQ(doc1.window.lines_observed, 2U);
    EXPECT_FALSE(doc1.stability.has_value());

    engine.open_window(t0 + std::chrono::seconds(2));
    engine.ingest_event(make_event("b"));
    auto doc2{engine.close_window(t0 + std::chrono::seconds(3))};
    EXPECT_EQ(doc2.window.lines_observed, 1U);
    EXPECT_EQ(doc2.stats.unique_templates, 1U);
    ASSERT_TRUE(doc2.stability.has_value());
    EXPECT_EQ(doc2.stability->new_templates, 1U);
    EXPECT_EQ(doc2.stability->vanished_templates, 1U);
}

// ── Window block ──────────────────────────────────────────────────────────────

TEST(MetaLogEngineWindow, DurationAndLinesObserved)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    auto t1{t0 + std::chrono::seconds(300)};
    engine.open_window(t0);
    for (int i = 0; i < 7; ++i)
        engine.ingest_event(make_event("x"));
    auto doc{engine.close_window(t1)};
    EXPECT_EQ(doc.window.lines_observed, 7U);
    EXPECT_EQ(doc.window.duration_seconds, 300U);
}

TEST(MetaLogEngineWindow, StartAndEndISONotEmpty)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("x"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    EXPECT_FALSE(doc.window.start_iso.empty());
    EXPECT_FALSE(doc.window.end_iso.empty());
    EXPECT_NE(doc.window.start_iso, doc.window.end_iso);
}
// ── Lifecycle guards ──────────────────────────────────────────────────────────

TEST(MetaLogEngineLifecycle, IngestBeforeOpenWindowThrows)
{
    meta::MetaLogEngine engine;
    EXPECT_THROW(engine.ingest_event(make_event("x")), std::logic_error);
}

TEST(MetaLogEngineLifecycle, CloseBeforeOpenWindowThrows)
{
    meta::MetaLogEngine engine;
    EXPECT_THROW((void)engine.close_window(std::chrono::system_clock::now()), std::logic_error);
}

} // namespace

// NOLINTEND
