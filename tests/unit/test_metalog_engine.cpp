// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Unit tests for the v0.2.0 spec-conformant MetaLogEngine.

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "insight/metalog/metalog_engine.hpp"
#include "insight/tokenization/canonical_event.hpp"

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

tok::CanonicalEvent make_event(std::string_view tmpl,
                               insight::LogLevel level = insight::LogLevel::Info)
{
    tok::CanonicalEvent ev;
    ev.template_str = tmpl;
    ev.level = level;
    return ev;
}

// ── Helper: CanonicalEvent with owned param values ────────────────────────────
//
// CanonicalEvent::params is a span<const string_view> into arena-stable storage.
// In tests (no arena) we own the strings here; the views and span stay valid
// for the lifetime of ParamEvent.
struct ParamEvent
{
    std::vector<std::string> owned_values;
    std::vector<std::string_view> views;
    tok::CanonicalEvent event;

    static ParamEvent make(std::string_view tmpl, std::initializer_list<std::string_view> params,
                           insight::LogLevel level = insight::LogLevel::Info)
    {
        ParamEvent p;
        p.owned_values.reserve(params.size());
        p.views.reserve(params.size());
        for (auto s : params)
        {
            p.owned_values.emplace_back(s);
            p.views.push_back(p.owned_values.back());
        }
        p.event.template_str = tmpl;
        p.event.params = p.views;
        p.event.level = level;
        return p;
    }
};

class BehaviorBlockTest : public ::testing::Test
{
  protected:
    insight::Timestamp start_ = std::chrono::system_clock::now();
};

TEST_F(BehaviorBlockTest, EmitsBigramsByDefault)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);

    // Sequence A,B,A,B,A => 4 bigrams across 2 distinct keys.
    auto a{make_event("alpha <*>")};
    auto b{make_event("beta <*>")};
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);

    auto doc{engine.close_window(start_ + std::chrono::seconds(60))};

    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_EQ(doc.behavior->ngram_size, 2U);
    EXPECT_EQ(doc.behavior->top_ngrams.size(), 2U); // A->B, B->A
    EXPECT_EQ(doc.behavior->top_ngrams.front().sequence.size(), 2U);
}

TEST_F(BehaviorBlockTest, ProbabilityIsConditionalOnPrefix)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);
    auto a{make_event("alpha")};
    auto b{make_event("beta")};
    auto c{make_event("gamma")};
    // A,B,A,C => bigrams A->B (1), B->A (1), A->C (1). p(B|A) = 0.5,
    // p(C|A) = 0.5, p(A|B) = 1.0.
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(c);
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};

    ASSERT_TRUE(doc.behavior.has_value());
    const auto a_id{meta::MetaLogEngine::compute_template_id("alpha")};
    const auto b_id{meta::MetaLogEngine::compute_template_id("beta")};
    bool found_b_to_a = false;
    bool found_a_to_b = false;
    for (const auto& ng : doc.behavior->top_ngrams)
    {
        if (ng.sequence.size() != 2)
            continue;
        if (ng.sequence[0] == b_id && ng.sequence[1] == a_id)
        {
            EXPECT_NEAR(ng.probability, 1.0, 1e-9);
            found_b_to_a = true;
        }
        if (ng.sequence[0] == a_id && ng.sequence[1] == b_id)
        {
            EXPECT_NEAR(ng.probability, 0.5, 1e-9);
            found_a_to_b = true;
        }
    }
    EXPECT_TRUE(found_b_to_a);
    EXPECT_TRUE(found_a_to_b);
}

TEST_F(BehaviorBlockTest, TrigramOrderRespected)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .ngram_size = 3,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    engine.ingest_event(make_event("c"));
    engine.ingest_event(make_event("d"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_EQ(doc.behavior->ngram_size, 3U);
    ASSERT_FALSE(doc.behavior->top_ngrams.empty());
    EXPECT_EQ(doc.behavior->top_ngrams.front().sequence.size(), 3U);
}

TEST_F(BehaviorBlockTest, TopNgramsSizeZeroOmitsBlock)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 0,
    }};
    engine.open_window(start_);
    engine.ingest_event(make_event("x"));
    engine.ingest_event(make_event("y"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    EXPECT_FALSE(doc.behavior.has_value());
}

TEST_F(BehaviorBlockTest, BoundedNgramKeysCapDistinctEntries)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 16,
        .top_ngrams_size = 16,
        .max_ngram_keys = 4, // tiny cap
    }};
    engine.open_window(start_);
    for (int i = 0; i < 20; ++i)
        engine.ingest_event(make_event(std::string{"t"} + std::to_string(i)));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_LE(doc.behavior->top_ngrams.size(), 4U);
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

// ── FieldHistogramDiffTest ────────────────────────────────────────────────────
//
// TDD: verify that MetaLogDiff::field_histogram_deltas carries per-param
// JS divergence when histograms are enabled.  RED before diff() is updated,
// GREEN after.

namespace
{
    // Make a single-template doc with Bernoulli(p500) status distribution.
    // Template: "GET <*> -> <*>", 2 params tracked (method and status_code).
    meta::MetaLogDocument make_status_doc(insight::Timestamp start, insight::Timestamp end,
                                          std::size_t count_200, std::size_t count_500)
    {
        meta::MetaLogConfig cfg;
        cfg.max_param_histograms = 2;
        meta::MetaLogEngine eng{cfg};
        eng.open_window(start);
        // params[0] = path (constant), params[1] = status_code (varies).
        // max_param_histograms=2 tracks both; the status_code distribution
        // is what we expect to diverge between windows.
        auto ev_200 = ParamEvent::make("GET <*> -> <*>", {"/api/users", "200"});
        auto ev_500 = ParamEvent::make("GET <*> -> <*>", {"/api/users", "500"});
        for (std::size_t i = 0; i < count_200; ++i)
            eng.ingest_event(ev_200.event);
        for (std::size_t i = 0; i < count_500; ++i)
            eng.ingest_event(ev_500.event);
        return eng.close_window(end);
    }
} // namespace

// Regression guard: default config produces no field_histogram_deltas.
// diff() must not crash or invent deltas when histograms are disabled.
TEST(FieldHistogramDiffTest, EmptyWhenHistogramsDisabled)
{
    const auto t0 = insight::Timestamp{} + std::chrono::hours{1};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    meta::MetaLogEngine eng; // default config: max_param_histograms=0
    eng.open_window(t0);
    auto ev_a = ParamEvent::make("GET <*>", {"200"});
    for (int i = 0; i < 50; ++i)
        eng.ingest_event(ev_a.event);
    auto prev = eng.close_window(t1);

    eng.open_window(t1);
    auto ev_b = ParamEvent::make("GET <*>", {"500"});
    for (int i = 0; i < 50; ++i)
        eng.ingest_event(ev_b.event);
    auto curr = eng.close_window(t2);

    auto d = meta::diff(prev, curr);
    EXPECT_TRUE(d.field_histogram_deltas.empty())
        << "field_histogram_deltas must be empty when max_param_histograms=0";
}

// Core contract: diff between a normal window (80×200, 20×500) and a
// degraded window (20×200, 80×500) must produce a non-zero JS divergence
// for the status_code param (index 1 for our 2-param setup).
//
// This is the RED test — it requires FieldHistogramDelta to be populated
// by diff(). Before the implementation, field_histogram_deltas will be
// empty and the assertion fails.
TEST(FieldHistogramDiffTest, JSDivergenceNonZeroAfterStatusFlip)
{
    const auto t0 = insight::Timestamp{} + std::chrono::hours{2};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    auto prev = make_status_doc(t0, t1, /*200=*/80, /*500=*/20);
    auto curr = make_status_doc(t1, t2, /*200=*/20, /*500=*/80);

    auto d = meta::diff(prev, curr);

    ASSERT_FALSE(d.field_histogram_deltas.empty())
        << "Expected field_histogram_deltas after status distribution flip "
           "(requires max_param_histograms > 0 in both docs)";

    // param_index=1 (status_code in our 2-slot setup) must show the largest shift.
    bool found_status_delta = false;
    for (const auto& fhd : d.field_histogram_deltas)
    {
        if (fhd.param_index == 1)
        {
            EXPECT_GT(fhd.js_divergence, 0.0)
                << "Expected non-zero JS divergence for the status_code flip "
                   "(P=[0.8,0.2] vs Q=[0.2,0.8])";
            found_status_delta = true;
        }
    }
    EXPECT_TRUE(found_status_delta)
        << "Expected FieldHistogramDelta for param_index=1 (status_code)";
}

// Stability contract: identical distribution in both windows produces
// near-zero JS divergence in field_histogram_deltas.
TEST(FieldHistogramDiffTest, JSDivergenceNearZeroForSameDistribution)
{
    const auto t0 = insight::Timestamp{} + std::chrono::hours{3};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    auto prev = make_status_doc(t0, t1, /*200=*/80, /*500=*/20);
    auto curr = make_status_doc(t1, t2, /*200=*/80, /*500=*/20); // same distribution

    auto d = meta::diff(prev, curr);

    // Even with Laplace smoothing, identical value_counts maps give JSD = 0 exactly.
    for (const auto& fhd : d.field_histogram_deltas)
    {
        EXPECT_LT(fhd.js_divergence, 0.01)
            << "JS divergence must be near-zero for identical distributions "
               "(param_index="
            << fhd.param_index << ")";
    }
}

// ── HyperLogLog cardinality tests ────────────────────────────────────────────
//
// Verify that FieldHistogram::approximate_cardinality is populated by HLL
// and that FieldHistogramDelta::cardinality_delta captures the growth.
//
// These tests verify that limitation §2 item 4 is lifted: cardinality
// estimation now works correctly even when value_counts table is capped.

TEST(HllCardinalityTest, ApproximateCardinalityIsNonZeroWhenHistogramsEnabled)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 1;
    cfg.max_histogram_values = 10;
    meta::MetaLogEngine eng{cfg};

    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};

    eng.open_window(t0);
    // Inject 50 distinct values — value table caps at 10, but HLL sees all 50.
    for (int i = 0; i < 50; ++i)
    {
        auto e = ParamEvent::make("GET <*>", {"val_" + std::to_string(i)});
        eng.ingest_event(e.event);
    }
    auto doc = eng.close_window(t1);

    ASSERT_FALSE(doc.stats.top_k.empty());
    const auto& entry = doc.stats.top_k[0];
    ASSERT_FALSE(entry.field_histograms.empty());
    const auto& fh = entry.field_histograms[0];

    EXPECT_GT(fh.approximate_cardinality, 0u)
        << "HLL approximate_cardinality must be non-zero after 50 distinct values";
    // HLL at p=14 has ~1.5% error; 50 distinct values → estimate should be
    // within a reasonable range (5..200) even for small cardinalities.
    EXPECT_GE(fh.approximate_cardinality, 5u) << "HLL estimate too low for 50 distinct values";
}

TEST(HllCardinalityTest, ApproximateCardinalityIsZeroWhenHistogramsDisabled)
{
    meta::MetaLogEngine eng; // default: max_param_histograms = 0
    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};

    eng.open_window(t0);
    auto e = ParamEvent::make("GET <*>", {"value"});
    eng.ingest_event(e.event);
    auto doc = eng.close_window(t1);

    for (const auto& entry : doc.stats.top_k)
        for (const auto& fh : entry.field_histograms)
            EXPECT_EQ(fh.approximate_cardinality, 0u)
                << "HLL must not run when max_param_histograms=0";
}

TEST(HllCardinalityTest, EstimatesHighCardinalityEvenWhenValueTableFull)
{
    // This is the key cardinality-limitation test: with max_histogram_values=10,
    // the value_counts table fills up after 10 unique values and cannot track
    // more. But HLL sketches ALL values and still gives a good estimate.

    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 1;
    cfg.max_histogram_values = 10; // table caps at 10

    meta::MetaLogEngine eng{cfg};
    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};

    eng.open_window(t0);
    // Inject 500 distinct values.
    for (int i = 0; i < 500; ++i)
    {
        auto e = ParamEvent::make("GET <*>", {"uid_" + std::to_string(i)});
        eng.ingest_event(e.event);
    }
    auto doc = eng.close_window(t1);

    const auto& fh = doc.stats.top_k[0].field_histograms[0];

    // value_counts should have at most 10 entries.
    EXPECT_LE(fh.value_counts.size(), 10u)
        << "value_counts table must be capped at max_histogram_values";

    // HLL estimate should be much larger — demonstrating the limitation is lifted.
    EXPECT_GT(fh.approximate_cardinality, 50u)
        << "HLL must track cardinality beyond the value_counts cap. "
           "This verifies that limitation §2 item 4 (blind cardinality explosion) "
           "is lifted: approximate_cardinality reflects the real-world count even "
           "when the exact value table was full.";
}

TEST(HllCardinalityTest, CardinalityDeltaPopulatedInDiff)
{
    // Build two windows: first with 20 distinct values, second with 100.
    // The diff must carry a positive cardinality_delta for the param.

    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 1;
    cfg.max_histogram_values = 200;

    meta::MetaLogEngine eng{cfg};
    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    eng.open_window(t0);
    for (int i = 0; i < 20; ++i)
        eng.ingest_event(ParamEvent::make("GET <*>", {"v" + std::to_string(i)}).event);
    auto doc1 = eng.close_window(t1);

    eng.open_window(t1);
    for (int i = 0; i < 100; ++i)
        eng.ingest_event(ParamEvent::make("GET <*>", {"v" + std::to_string(i)}).event);
    auto doc2 = eng.close_window(t2);

    auto d = meta::diff(doc1, doc2);

    ASSERT_FALSE(d.field_histogram_deltas.empty())
        << "field_histogram_deltas must be populated when histograms are enabled";

    bool found_positive_delta = false;
    for (const auto& fhd : d.field_histogram_deltas)
    {
        if (fhd.current_cardinality > 0 || fhd.previous_cardinality > 0)
        {
            // The second window has more distinct values → delta should be positive.
            if (fhd.cardinality_delta > 0)
                found_positive_delta = true;
        }
    }
    EXPECT_TRUE(found_positive_delta)
        << "FieldHistogramDelta::cardinality_delta must be positive when the second "
           "window contains more distinct param values than the first.";
}

} // namespace

// NOLINTEND
