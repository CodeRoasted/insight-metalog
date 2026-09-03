// Unit tests: allow short identifiers and test-specific patterns
// StabilityBlock: cross-window stability score, KL/JS divergences, new/vanished counts.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

class StabilityBlockTest : public ::testing::Test
{
  protected:
    insight::Timestamp t0_ = std::chrono::system_clock::now();
};

TEST_F(StabilityBlockTest, FirstWindowHasNoStabilityBlock)
{
    meta::MetaLogEngine engine;
    engine.open_window(t0_);
    engine.ingest_event(make_event("a"));
    auto doc{engine.close_window(t0_ + std::chrono::seconds(1))};
    EXPECT_FALSE(doc.stability.has_value());
}

TEST_F(StabilityBlockTest, IdenticalDistributionsScoreNearOne)
{
    meta::MetaLogEngine engine;
    engine.open_window(t0_);
    for (int i = 0; i < 50; ++i)
    {
        engine.ingest_event(make_event("a"));
        engine.ingest_event(make_event("b"));
    }
    auto d1{engine.close_window(t0_ + std::chrono::seconds(60))};

    engine.open_window(t0_ + std::chrono::seconds(60));
    for (int i = 0; i < 50; ++i)
    {
        engine.ingest_event(make_event("a"));
        engine.ingest_event(make_event("b"));
    }
    auto d2{engine.close_window(t0_ + std::chrono::seconds(120))};
    ASSERT_TRUE(d2.stability.has_value());
    EXPECT_EQ(d2.stability->previous_window_end_iso, d1.window.end_iso);
    EXPECT_EQ(d2.stability->new_templates, 0U);
    EXPECT_EQ(d2.stability->vanished_templates, 0U);
    EXPECT_LT(d2.stability->js_divergence, 1e-6);
    EXPECT_GT(d2.stability->stability_score, 0.999);
}

TEST_F(StabilityBlockTest, NewAndVanishedAreCounted)
{
    meta::MetaLogEngine engine;
    engine.open_window(t0_);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    (void)engine.close_window(t0_ + std::chrono::seconds(60));

    engine.open_window(t0_ + std::chrono::seconds(60));
    engine.ingest_event(make_event("a")); // b vanished, c new
    engine.ingest_event(make_event("c"));
    auto d2{engine.close_window(t0_ + std::chrono::seconds(120))};
    ASSERT_TRUE(d2.stability.has_value());
    EXPECT_EQ(d2.stability->new_templates, 1U);
    EXPECT_EQ(d2.stability->vanished_templates, 1U);
    EXPECT_GT(d2.stability->js_divergence, 0.0);
    EXPECT_LT(d2.stability->stability_score, 1.0);
}

TEST_F(StabilityBlockTest, DivergencesAreNonNegative)
{
    meta::MetaLogEngine engine;
    engine.open_window(t0_);
    engine.ingest_event(make_event("a"));
    (void)engine.close_window(t0_ + std::chrono::seconds(1));

    engine.open_window(t0_ + std::chrono::seconds(1));
    engine.ingest_event(make_event("a"));
    auto d2{engine.close_window(t0_ + std::chrono::seconds(2))};
    ASSERT_TRUE(d2.stability.has_value());
    EXPECT_GE(d2.stability->kl_divergence, 0.0);
    EXPECT_GE(d2.stability->js_divergence, 0.0);
    EXPECT_LE(d2.stability->js_divergence, 1.0);
}
// ── Stability extras ──────────────────────────────────────────────────────────

TEST(MetaLogEngineStability, EmitStabilityFalseNeverEmitsBlock)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.emit_stability = false}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("a"));
    (void)engine.close_window(t0 + std::chrono::seconds(1));

    engine.open_window(t0 + std::chrono::seconds(1));
    engine.ingest_event(make_event("a"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(2))};
    EXPECT_FALSE(doc.stability.has_value());
}

TEST(MetaLogEngineStability, StabilityScoreAlwaysInZeroOne)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 20; ++i)
        engine.ingest_event(make_event("a"));
    (void)engine.close_window(t0 + std::chrono::seconds(1));

    // Completely disjoint next window.
    engine.open_window(t0 + std::chrono::seconds(1));
    for (int i = 0; i < 20; ++i)
        engine.ingest_event(make_event("z"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(2))};
    ASSERT_TRUE(doc.stability.has_value());
    EXPECT_GE(doc.stability->stability_score, 0.0);
    EXPECT_LE(doc.stability->stability_score, 1.0);
    EXPECT_GE(doc.stability->js_divergence, 0.0);
    EXPECT_LE(doc.stability->js_divergence, 1.0);
}

} // namespace
