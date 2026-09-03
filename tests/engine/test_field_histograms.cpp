// Unit tests: allow short identifiers and test-specific patterns
// FieldHistogram collection: per-param value counts, entropy, bounded value tables.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// ── Field histogram tests ─────────────────────────────────────────────────────
//
// These tests capture the "Drain wildcarding / representation collapse" design
// constraint and verify that FieldHistogram correctly re-surfaces the causal
// structure that Drain removes.
//
// TDD structure:
//   Test 1 — regression guard: default config emits NO histograms (zero
//             overhead guarantee).  Passes before and after implementation.
//   Tests 2–5 — RED before engine impl, GREEN after.

// Test 1 (regression guard): default config → field_histograms empty.
//
// This test documents the current limitation: with max_param_histograms=0
// (default), the status_code distribution "500" vs "200" is invisible to
// MetaLog, JS divergence collapses to ~0, and the drift/sequence banks
// receive no causal signal. This is the state before the feature.
TEST(FieldHistogramTest, DisabledByDefault_ParamsDiscarded)
{
    meta::MetaLogEngine engine; // default config: max_param_histograms = 0
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    // 80 % 500-error traffic — mirrors the spike scenario.
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
    // Default: no field histograms regardless of params content.
    // The status_code distribution is not observable downstream.
    EXPECT_TRUE(doc.stats.top_k[0].field_histograms.empty())
        << "Default config must produce no field histograms (zero-overhead guarantee). "
           "This is the documented limitation: Drain wildcarding removes the causal axis.";
}

// Test 2 (RED before impl): enabled config collects per-param value counts.
//
// With max_param_histograms=2, MetaLog must accumulate the empirical
// distribution P(value | template_id, param_index) for params[0] and params[1].
// This directly demonstrates the improvement over the default:
//   Before: drift banks see identical frequency vectors -> zero JS divergence
//   After:  drift banks can observe P("500" | GET) shifting across windows
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

    // Two histogram slots for the two wildcard positions.
    ASSERT_EQ(entry.field_histograms.size(), 2U);

    // params[0] = path — always "/api/users" -> one value, 100 total.
    const auto& path_hist{entry.field_histograms[0]};
    EXPECT_EQ(path_hist.param_index, 0U);
    EXPECT_EQ(path_hist.total, 100U);
    ASSERT_EQ(path_hist.value_counts.size(), 1U);
    EXPECT_EQ(path_hist.value_counts.at("/api/users"), 100U);

    // params[1] = status_code — "500": 80, "200": 20.
    const auto& status_hist{entry.field_histograms[1]};
    EXPECT_EQ(status_hist.param_index, 1U);
    EXPECT_EQ(status_hist.total, 100U);
    ASSERT_EQ(status_hist.value_counts.size(), 2U);
    EXPECT_EQ(status_hist.value_counts.at("500"), 80U);
    EXPECT_EQ(status_hist.value_counts.at("200"), 20U);
}

// Test 3 (RED before impl): entropy is 0 when all events share the same value.
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

// Test 4 (RED before impl): entropy equals log2(k) for a uniform k-valued param.
TEST(FieldHistogramTest, Enabled_EntropyMaxForUniformDistribution)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    // 4 equally-likely values, 25 each -> entropy = log2(4) = 2.0 bits.
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

// Test 5 (RED before impl): value table is bounded by max_histogram_values.
//
// When > max_histogram_values distinct values are observed:
//   - value_counts.size() <= max_histogram_values
//   - total still reflects all observed events (not just tracked ones)
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

// ── FieldHistogramSerializationTest ───────────────────────────────────────────
//
// Measure-first gate (a) for the `param_histograms` wire field. The domain
// FieldHistogram::value_counts is an unordered_map (iteration order not portable
// across runs/impls), so emission MUST key-sort or replay bit-identity is lost.

} // namespace
