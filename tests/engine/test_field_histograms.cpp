#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

TEST(FieldHistogramTest, DisabledByDefault_ParamsDiscarded)
{
    meta::MetaLogEngine engine;
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    for (int i = 0; i < 80; ++i)
    {
        auto ev{ParamEvent::make("GET <*> -> <*>", {"/api/users", "500"})};
        engine.ingest_event(ev.event);
    }
    for (int i = 0; i < 20; ++i)
    {
        auto ev{ParamEvent::make("GET <*> -> <*>", {"/api/users", "200"})};
        engine.ingest_event(ev.event);
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    EXPECT_TRUE(doc.stats.top_k[0].field_histograms.empty())
        << "Default config must produce no field histograms (zero-overhead guarantee). "
           "This is the documented limitation: Drain wildcarding removes the causal axis.";
}

TEST(FieldHistogramTest, Enabled_CollectsParamValueCounts)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .max_param_histograms = 2,
    }};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    for (int i = 0; i < 80; ++i)
    {
        auto ev{ParamEvent::make("GET <*> -> <*>", {"/api/users", "500"})};
        engine.ingest_event(ev.event);
    }
    for (int i = 0; i < 20; ++i)
    {
        auto ev{ParamEvent::make("GET <*> -> <*>", {"/api/users", "200"})};
        engine.ingest_event(ev.event);
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    const auto& entry{doc.stats.top_k[0]};

    ASSERT_EQ(entry.field_histograms.size(), 2U);

    const auto& path_hist{entry.field_histograms[0]};
    EXPECT_EQ(path_hist.param_index, 0U);
    EXPECT_EQ(path_hist.total, 100U);
    ASSERT_EQ(path_hist.value_counts.size(), 1U);
    EXPECT_EQ(path_hist.value_counts.at("/api/users"), 100U);

    const auto& status_hist{entry.field_histograms[1]};
    EXPECT_EQ(status_hist.param_index, 1U);
    EXPECT_EQ(status_hist.total, 100U);
    ASSERT_EQ(status_hist.value_counts.size(), 2U);
    EXPECT_EQ(status_hist.value_counts.at("500"), 80U);
    EXPECT_EQ(status_hist.value_counts.at("200"), 20U);
}

TEST(FieldHistogramTest, Enabled_EntropyZeroForConstantParam)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    for (int i = 0; i < 100; ++i)
    {
        auto ev{ParamEvent::make("action=<*>", {"login"})};
        engine.ingest_event(ev.event);
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    ASSERT_EQ(doc.stats.top_k[0].field_histograms.size(), 1U);
    EXPECT_NEAR(doc.stats.top_k[0].field_histograms[0].entropy_bits, 0.0, 1e-9)
        << "Constant param must have zero entropy.";
}

TEST(FieldHistogramTest, Enabled_EntropyMaxForUniformDistribution)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    for (int round{0}; round < 25; ++round)
    {
        for (auto val : {"a", "b", "c", "d"})
        {
            auto ev{ParamEvent::make("key=<*>", {val})};
            engine.ingest_event(ev.event);
        }
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    ASSERT_EQ(doc.stats.top_k[0].field_histograms.size(), 1U);
    EXPECT_NEAR(doc.stats.top_k[0].field_histograms[0].entropy_bits, 2.0, 1e-6)
        << "Uniform 4-valued distribution must have entropy = log2(4) = 2.0 bits.";
}

TEST(FieldHistogramTest, Enabled_ValueTableBoundedByMaxHistogramValues)
{
    constexpr std::size_t kMaxValues{10};
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .max_param_histograms = 1,
        .max_histogram_values = kMaxValues,
    }};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    for (int i{0}; i < 100; ++i)
    {
        auto ev{ParamEvent::make("id=<*>", {std::to_string(i)})};
        engine.ingest_event(ev.event);
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    ASSERT_EQ(doc.stats.top_k[0].field_histograms.size(), 1U);
    const auto& hist{doc.stats.top_k[0].field_histograms[0]};
    EXPECT_LE(hist.value_counts.size(), kMaxValues)
        << "value_counts must not exceed max_histogram_values.";
    EXPECT_EQ(hist.total, 100U) << "total must count all events, not only the tracked values.";
}

} // namespace
