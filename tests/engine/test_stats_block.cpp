#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

// invariant: the engine's entropy is a 40-fractional-bit fixed-point reduction, a few 2^-40 units
// from the real value, so 1e-9 bounds it with room and still reds a normalisation off by one.
constexpr double kEntropyTolerance{1e-9};

// post: the Shannon entropy in bits of a count vector, sum of p * log2(1 / p) — the reference
// the fixed-point reduction approximates, computed in the test and never in the engine.
[[nodiscard]] double entropy_bits_of(std::initializer_list<std::uint64_t> counts)
{
    double total{0.0};
    for (const std::uint64_t count : counts)
        total += static_cast<double>(count);
    double bits{0.0};
    for (const std::uint64_t count : counts)
    {
        const double share{static_cast<double>(count) / total};
        bits -= share * std::log2(share);
    }
    return bits;
}

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

// refs: F-SRC-insight-canon:canon.detail.mask.cppm:StatelessTemplate
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

TEST(MetaLogEngineStats, TailCountAndUnique)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    engine.ingest_event(make_event("c"));
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
    EXPECT_EQ(doc.stats.unique_templates, 1U);
    EXPECT_EQ(doc.stats.tail_unique, 1U);
}

// invariant: tail_summary is absent when the tail is empty and otherwise carries all three fields;
// a partial one is never emitted.
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
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 1, .top_ngrams_size = 0}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("a"));
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(make_event("b"));
    engine.ingest_event(make_event("c"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.stats.tail_summary.has_value());
    EXPECT_EQ(doc.stats.tail_summary->tail_template_count, doc.stats.tail_unique);
    EXPECT_EQ(doc.stats.tail_summary->tail_template_count, 2U);
    EXPECT_DOUBLE_EQ(doc.stats.tail_summary->tail_max_rate, 3.0 / 14.0);
    EXPECT_NEAR(doc.stats.tail_summary->tail_entropy_bits, entropy_bits_of({3, 1}),
                kEntropyTolerance)
        << "the tail is {3, 1}, so its entropy is the two-outcome entropy at 3/4";
}

TEST(MetaLogEngineStats, TailSummaryEntropyCollapsesWhenOneTemplateDominatesTail)
{
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
    // invariant: both values are DERIVED from the fixture — the tail is {98, 1, 1} over 300 lines —
    // so a normalisation off by one count is red, which a bound with headroom never was.
    EXPECT_NEAR(doc.stats.tail_summary->tail_entropy_bits, entropy_bits_of({98, 1, 1}),
                kEntropyTolerance)
        << "entropy is normalised over the tail's own mass (100), never over lines_observed";
    EXPECT_DOUBLE_EQ(doc.stats.tail_summary->tail_max_rate, 98.0 / 300.0)
        << "the max rate is the top tail template's count over lines_observed, never over the tail";
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
