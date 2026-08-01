// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// StatsBlock: entropy, unique templates, top-K ordering, tail summary, frequency normalisation.

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

class StatsBlockTest : public ::testing::Test
{
  protected:
    insight::Timestamp start_ = std::chrono::system_clock::now();
};

TEST_F(StatsBlockTest, EntropyBitsZeroForSingleTemplate)
{
    meta::MetaLogEngine engine;
    engine.open_window(start_);
    engine.ingest_event(make_event("only"));
    engine.ingest_event(make_event("only"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.stats.entropy_bits.has_value());
    EXPECT_NEAR(*doc.stats.entropy_bits, 0.0, 1e-9);
}

TEST_F(StatsBlockTest, EntropyBitsOneForUniformBinary)
{
    meta::MetaLogEngine engine;
    engine.open_window(start_);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.stats.entropy_bits.has_value());
    EXPECT_NEAR(*doc.stats.entropy_bits, 1.0, 1e-9);
}

// ── Stats block ───────────────────────────────────────────────────────────────

TEST(MetaLogEngineStats, UniqueTemplateCount)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("c"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    EXPECT_EQ(doc.stats.unique_templates, 3U);
}

TEST(MetaLogEngineStats, TopKOrderedByCountDesc)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 10}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(make_event("rare"));
    for (int i = 0; i < 20; ++i)
        engine.ingest_event(make_event("common"));
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("medium"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_GE(doc.stats.top_k.size(), 3U);
    EXPECT_GE(doc.stats.top_k[0].count, doc.stats.top_k[1].count);
    EXPECT_GE(doc.stats.top_k[1].count, doc.stats.top_k[2].count);
    EXPECT_EQ(doc.stats.top_k[0].count, 20U);
}

// (The former EvolvingClusterTemplateStaysOneBucket test is retired: the stateless
// masker makes template_str a pure function of a line's own tokens, so a template
// CANNOT evolve mid-window — two different template_str values are two distinct
// identities by construction, never a "same cluster, evolved template" merge. The
// migrate_bucket path it exercised was deleted with the Drain clustering, and the
// correctness it protected is now guaranteed at the source
// SRC-D-TID-3; the canon phantom-pair property tests pin the run-independence directly.)

TEST(MetaLogEngineStats, TailCountAndUnique)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b")); // goes to tail
    engine.ingest_event(make_event("c")); // goes to tail
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_EQ(doc.stats.top_k.size(), 1U);
    EXPECT_EQ(doc.stats.tail_unique, 2U);
    EXPECT_EQ(doc.stats.tail_count, 2U);
}

TEST(MetaLogEngineStats, TopKSizeZeroGivesEmptyTopK)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 0, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("x"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    EXPECT_TRUE(doc.stats.top_k.empty());
    EXPECT_EQ(doc.stats.unique_templates, 1U); // still counted
    EXPECT_EQ(doc.stats.tail_unique, 1U);
}

// SPEC §3.6 (MetaLog 0.3): tail_summary is absent when the tail is
// empty (everything fits in top_k), and present + populated otherwise.
TEST(MetaLogEngineStats, TailSummaryAbsentWhenTailEmpty)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 10, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(make_event("a"));
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(make_event("b"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    EXPECT_EQ(doc.stats.tail_unique, 0U);
    EXPECT_FALSE(doc.stats.tail_summary.has_value());
}

TEST(MetaLogEngineStats, TailSummaryPresentAndMaxRateMatchesTopTailTemplate)
{
    // top_k = 1 forces b/c/d/e into the tail. `b` is the loudest tail
    // template (3 events out of 14 total lines).
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("a")); // top_k
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(make_event("b")); // tail (max)
    engine.ingest_event(make_event("c"));     // tail
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.stats.tail_summary.has_value());
    EXPECT_EQ(doc.stats.tail_summary->tail_template_count, doc.stats.tail_unique);
    EXPECT_EQ(doc.stats.tail_summary->tail_template_count, 2U);
    EXPECT_DOUBLE_EQ(doc.stats.tail_summary->tail_max_rate, 3.0 / 14.0);
    // Tail = {b:3, c:1}; H = -[(3/4) log2(3/4) + (1/4) log2(1/4)] ≈ 0.811.
    EXPECT_GT(doc.stats.tail_summary->tail_entropy_bits, 0.7);
    EXPECT_LT(doc.stats.tail_summary->tail_entropy_bits, 0.9);
}

TEST(MetaLogEngineStats, TailSummaryEntropyCollapsesWhenOneTemplateDominatesTail)
{
    // Three tail templates but one dominates 98/100 of the tail mass;
    // entropy should collapse far below the uniform bound (log2(3)≈1.58).
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 200; ++i)
        engine.ingest_event(make_event("dominant_topk"));
    for (int i = 0; i < 98; ++i)
        engine.ingest_event(make_event("dominant_tail"));
    engine.ingest_event(make_event("noise_a"));
    engine.ingest_event(make_event("noise_b"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.stats.tail_summary.has_value());
    EXPECT_LT(doc.stats.tail_summary->tail_entropy_bits, 0.4); // tail is concentrated
    EXPECT_GT(doc.stats.tail_summary->tail_max_rate, 0.32);    // 98/300 ≈ 0.327
}

TEST(MetaLogEngineStats, TailSummarySerialisedToJsonAtomically)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    const std::string json = meta::to_json(doc, engine.registry());

    auto parsed = glz::read_json<glz::generic>(json);
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << json;
    ASSERT_TRUE(parsed->contains("stats")) << json;
    auto& stats = (*parsed)["stats"];
    ASSERT_TRUE(stats.is_object()) << json;
    ASSERT_TRUE(stats.contains("tail_summary")) << json;
    // Atomic block: when present, all three fields are emitted together.
    auto& ts = stats["tail_summary"];
    EXPECT_TRUE(ts.contains("tail_template_count")) << json;
    EXPECT_TRUE(ts.contains("tail_entropy_bits")) << json;
    EXPECT_TRUE(ts.contains("tail_max_rate")) << json;
}

TEST(MetaLogEngineStats, FrequencySumsToOne)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 100, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("a"));
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("b"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    double total{0.0};
    for (const auto& e : doc.stats.top_k)
        total += e.frequency;
    EXPECT_NEAR(total, 1.0, 1e-9);
}

} // namespace

// NOLINTEND
