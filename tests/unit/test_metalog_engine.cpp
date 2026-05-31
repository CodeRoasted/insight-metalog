// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Unit tests for the v0.5.0 spec-conformant MetaLogEngine.

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>
#include <picosha2.h>

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

TEST_F(BehaviorBlockTest, DiffDoesNotInventBranchingDeltaWhenComposedBaselineDropsBranching)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
        .top_branching_size = 8,
    }};

    engine.open_window(start_);
    const auto a{make_event("alpha")};
    const auto b{make_event("beta")};
    const auto c{make_event("gamma")};
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(c);
    const auto doc{engine.close_window(start_ + std::chrono::seconds(1))};

    ASSERT_TRUE(doc.behavior.has_value());
    ASSERT_TRUE(doc.behavior->branching.has_value());
    ASSERT_FALSE(doc.behavior->branching->empty());

    const auto composed{meta::compose(doc, doc)};
    ASSERT_TRUE(composed.behavior.has_value());
    // compose drops branching (cannot recompute from aggregated counts), so
    // it is absent — not present-but-empty.
    EXPECT_FALSE(composed.behavior->branching.has_value());

    const auto diff{meta::diff(composed, doc)};
    EXPECT_TRUE(diff.branching_delta.empty())
        << "A composed baseline has no comparable branching entropy rows; missing rows must not "
           "be treated as zero entropy.";
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

// A single Drain cluster (one real template_id) whose template EVOLVES mid-window —
// its first occurrence renders a literal value, then the position becomes a wildcard
// — must stay ONE template with the full count, not a literal singleton plus a
// wildcarded rest. The split would let a diff mis-read the literal first occurrence
// as a vanished/new line (and surface it for an error template via severity).
TEST(MetaLogEngineStats, EvolvingClusterTemplateStaysOneBucket)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 10}};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    const auto evt = [](std::string_view tmpl)
    {
        tok::CanonicalEvent ev;
        ev.template_id = 7; // one real Drain cluster id (>= 1)
        ev.template_str = tmpl;
        ev.level = insight::LogLevel::Error;
        return ev;
    };
    engine.ingest_event(evt("payment timeout txn=8821")); // first: rendered literal
    engine.ingest_event(evt("payment timeout txn=<*>"));  // evolved: position wildcarded
    engine.ingest_event(evt("payment timeout txn=<*>"));
    engine.ingest_event(evt("payment timeout txn=<*>"));
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    EXPECT_EQ(doc.stats.unique_templates, 1U)
        << "an evolving cluster split into multiple templates";
    ASSERT_GE(doc.stats.top_k.size(), 1U);
    EXPECT_EQ(doc.stats.top_k[0].count, 4U)
        << "the literal first occurrence was lost to a singleton bucket";
    EXPECT_EQ(doc.stats.top_k[0].template_str, "payment timeout txn=<*>");
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
    const std::string json = meta::to_json(doc);

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

// ── FieldHistogramSerializationTest ───────────────────────────────────────────
//
// Measure-first gate (a) for the §3.5 `param_histograms` wire field. The domain
// FieldHistogram::value_counts is an unordered_map (iteration order not portable
// across runs/impls), so emission MUST key-sort for §15.6 replay bit-identity.
// These pin the wire contract: keys lexicographically sorted regardless of
// insertion order, byte-stable across repeated serialisation, and OMITTED on the
// default (max_param_histograms = 0) path so non-histogram documents are
// byte-unchanged.
TEST(FieldHistogramSerializationTest, ValueCountsEmittedKeySorted)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    // Distinctive values that appear ONLY in value_counts, ingested OUT of sorted
    // order. An insertion- or hash-ordered emit would place them out of order;
    // the std::map conversion at the serialiser must restore lexicographic order.
    for (const auto* val : {"zzz_zebra", "aaa_alpha", "mmm_mango"})
    {
        auto ev{ParamEvent::make("slot=<*>", {val})};
        engine.ingest_event(ev.event);
    }
    const auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    const std::string json{meta::to_json(doc)};

    ASSERT_NE(json.find("\"param_histograms\""), std::string::npos)
        << "param_histograms must be emitted when max_param_histograms > 0.\n"
        << json;
    const auto pos_a{json.find("aaa_alpha")};
    const auto pos_m{json.find("mmm_mango")};
    const auto pos_z{json.find("zzz_zebra")};
    ASSERT_NE(pos_a, std::string::npos) << json;
    ASSERT_NE(pos_m, std::string::npos) << json;
    ASSERT_NE(pos_z, std::string::npos) << json;
    EXPECT_LT(pos_a, pos_m) << "value_counts must serialise key-sorted (§15.6).\n" << json;
    EXPECT_LT(pos_m, pos_z) << "value_counts must serialise key-sorted (§15.6).\n" << json;

    // §3.5 MUST NOT: no entropy_bits (a float, derivable from value_counts). Every
    // emitted field is integer-TYPED — param_index, value_counts counts, total, and
    // the HLL approximate_cardinality (=3 distinct values here) — so no float lands
    // on the wire. Pin the exact shape (key-sorted value_counts).
    EXPECT_NE(json.find("\"param_histograms\":[{\"param_index\":0,\"value_counts\":"
                        "{\"aaa_alpha\":1,\"mmm_mango\":1,\"zzz_zebra\":1},\"total\":3,"
                        "\"approximate_cardinality\":3}]"),
              std::string::npos)
        << "param_histograms must serialise integer-only & key-sorted (no entropy_bits).\n"
        << json;

    EXPECT_EQ(meta::to_json(doc), json) << "serialisation must be byte-identical on repeat.";
}

TEST(FieldHistogramSerializationTest, OmittedWhenDisabled)
{
    meta::MetaLogEngine engine; // default: max_param_histograms = 0
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i{0}; i < 5; ++i)
    {
        auto ev{ParamEvent::make("slot=<*>", {"v"})};
        engine.ingest_event(ev.event);
    }
    const auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    const std::string json{meta::to_json(doc)};

    EXPECT_EQ(json.find("param_histograms"), std::string::npos)
        << "param_histograms must be omitted on the default path (byte-unchanged docs).\n"
        << json;
}

// Measure-first gate (b): with the histogram caps in force (top_k_size ×
// max_param_histograms × max_histogram_values), the serialised overhead is
// bounded and acceptable for the batch / full-fidelity path. Builds a realistic
// CI-shaped batch (512 templates × 2 param slots, ~20 / ~6 distinct values),
// prints the measured doc sizes, and asserts the overhead stays within the
// cap-derived upper bound (param_histograms can never make the doc unbounded).
TEST(FieldHistogramSerializationTest, BoundedDocumentOverhead)
{
    constexpr std::size_t kTemplates{512};
    constexpr std::size_t kMaxHist{2};
    constexpr std::size_t kMaxValues{64};
    constexpr std::size_t kPaths{20};
    constexpr std::size_t kCodes{6};
    constexpr std::size_t kEventsPerTemplate{40};

    const auto build{[&](std::size_t max_param_histograms)
                     {
                         meta::MetaLogEngine engine{meta::MetaLogConfig{
                             .top_k_size = kTemplates,
                             .max_param_histograms = max_param_histograms,
                             .max_histogram_values = kMaxValues,
                         }};
                         const auto t0{std::chrono::system_clock::now()};
                         engine.open_window(t0);
                         for (std::size_t tmpl{0}; tmpl < kTemplates; ++tmpl)
                         {
                             const std::string tstr{"t" + std::to_string(tmpl) +
                                                    " path=<*> code=<*>"};
                             for (std::size_t ev{0}; ev < kEventsPerTemplate; ++ev)
                             {
                                 const std::string path{"/api/resource_" + std::to_string(ev % kPaths)};
                                 const std::string code{std::to_string(200 + (ev % kCodes) * 100)};
                                 auto pe{ParamEvent::make(tstr, {path, code})};
                                 engine.ingest_event(pe.event);
                             }
                         }
                         return engine.close_window(t0 + std::chrono::seconds(1));
                     }};

    const auto doc_with{build(kMaxHist)};
    const auto doc_without{build(0)};
    const std::string json_with{meta::to_json(doc_with)};
    const std::string json_without{meta::to_json(doc_without)};

    ASSERT_FALSE(doc_with.stats.top_k.empty());
    ASSERT_GT(json_with.size(), json_without.size()) << "histograms must add bytes";
    const std::size_t top_k_count{doc_with.stats.top_k.size()};
    const std::size_t overhead{json_with.size() - json_without.size()};

    std::cout << "[ SIZE ] templates=" << top_k_count << " doc_without=" << json_without.size()
              << "B doc_with=" << json_with.size() << "B overhead=" << overhead
              << "B per_template=" << (overhead / top_k_count) << "B overhead_pct="
              << (100.0 * static_cast<double>(overhead) / static_cast<double>(json_without.size()))
              << "%\n";

    // Cap-derived upper bound: every slot at max distinct values, generous bytes
    // per "key":count JSON entry. Real overhead is far below this; the guard is
    // that param_histograms can never make the batch document unbounded.
    constexpr std::size_t kBytesPerValueEntryUpperBound{96};
    const std::size_t cap_bound{top_k_count * kMaxHist * kMaxValues * kBytesPerValueEntryUpperBound};
    EXPECT_LE(overhead, cap_bound)
        << "param_histograms overhead must stay within the cap-derived bound. overhead=" << overhead
        << "B cap_bound=" << cap_bound << "B";
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

// tail_delta is populated only when BOTH documents carry a tail_summary, and
// carries before/after/delta for all three TailSummary fields. A tail going
// louder + more concentrated (the emerging-chronic-error signature) shows a
// positive max_rate delta and a negative entropy delta.
TEST(TailDeltaDiffTest, PopulatedWithLouderConcentratedTail)
{
    meta::MetaLogDocument prev;
    prev.window.lines_observed = 1000;
    prev.stats.tail_summary = meta::TailSummary{
        .tail_template_count = 40, .tail_entropy_bits = 4.0, .tail_max_rate = 0.001};

    meta::MetaLogDocument curr;
    curr.window.lines_observed = 1000;
    curr.stats.tail_summary = meta::TailSummary{
        .tail_template_count = 38, .tail_entropy_bits = 1.0, .tail_max_rate = 0.02};

    const auto d = meta::diff(prev, curr);

    ASSERT_TRUE(d.tail_delta.has_value())
        << "tail_delta must be present when both documents carry a tail_summary";
    const auto& t = *d.tail_delta;
    EXPECT_EQ(t.tail_template_count_delta, -2) << "40 -> 38";
    EXPECT_DOUBLE_EQ(t.tail_entropy_bits_delta, -3.0) << "4.0 -> 1.0: tail concentrating";
    EXPECT_DOUBLE_EQ(t.tail_max_rate_delta, 0.019) << "0.001 -> 0.02: tail getting louder";
}

TEST(TailDeltaDiffTest, AbsentWhenEitherDocLacksTailSummary)
{
    meta::MetaLogDocument with_tail;
    with_tail.window.lines_observed = 1000;
    with_tail.stats.tail_summary = meta::TailSummary{
        .tail_template_count = 10, .tail_entropy_bits = 2.0, .tail_max_rate = 0.005};
    meta::MetaLogDocument without_tail;
    without_tail.window.lines_observed = 1000; // no tail_summary set

    EXPECT_FALSE(meta::diff(with_tail, without_tail).tail_delta.has_value())
        << "a one-sided tail is appearance/vanishing, not a tail delta";
    EXPECT_FALSE(meta::diff(without_tail, with_tail).tail_delta.has_value());
}

// ── Salience Reservoir (Tier 2, F1) ──────────────────────────────────────────
//
// A rare-but-severe template that falls below top_k by frequency must survive in
// the reservoir (admitted by salience) instead of collapsing into the tail.

namespace
{
// Feed N frequent benign Info templates plus one rare event, with a small top_k
// so the rare event is below it. Returns the closed document.
meta::MetaLogDocument
run_with_rare_event(const tok::CanonicalEvent& rare, std::size_t top_k, std::size_t reservoir_size)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = top_k, .reservoir_size = reservoir_size, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha steady event"));
        engine.ingest_event(make_event("beta steady event"));
        engine.ingest_event(make_event("gamma steady event"));
        engine.ingest_event(make_event("delta steady event"));
    }
    engine.ingest_event(rare); // one occurrence — rank last by frequency
    return engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60});
}

[[nodiscard]] bool reservoir_has(const meta::MetaLogDocument& doc, std::string_view tmpl)
{
    return std::ranges::any_of(doc.stats.reservoir,
                               [&](const auto& entry) { return entry.template_str == tmpl; });
}
[[nodiscard]] bool top_k_has(const meta::MetaLogDocument& doc, std::string_view tmpl)
{
    return std::ranges::any_of(doc.stats.top_k,
                               [&](const auto& entry) { return entry.template_str == tmpl; });
}
} // namespace

TEST(ReservoirTest, RareErrorAdmittedBelowTopK)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, /*top_k=*/3, /*reservoir_size=*/8)};

    EXPECT_FALSE(top_k_has(doc, "connection refused to db"))
        << "the rare error is below top_k by frequency";
    ASSERT_TRUE(reservoir_has(doc, "connection refused to db"))
        << "F1: a rare severe event must survive in the salience reservoir, not the tail";
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_str == "connection refused to db")
        {
            EXPECT_GT(entry.salience, 0U);
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Error);
        }
}

TEST(ReservoirTest, TerminatorRoleIsSalient)
{
    auto rare{make_event("##[error]Process completed with exit code 2.", insight::LogLevel::Error)};
    rare.structural_role = insight::StructuralRole::Terminator;
    const auto doc{run_with_rare_event(rare, 3, 8)};
    ASSERT_TRUE(reservoir_has(doc, "##[error]Process completed with exit code 2."));
}

TEST(ReservoirTest, RareBenignNotAdmitted)
{
    // A rare INFO line with no failure signal scores 0 — benign rarity is chaff,
    // never admitted (the cache-shard-481 case).
    auto rare{make_event("Downloading cache shard chunk", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Downloading cache shard chunk"))
        << "rarity must never gate a benign template into the reservoir";
}

// F7 token-awareness: a benign INFO line whose text merely CONTAINS a failure word
// inside a token (a filename) must score 0 — the F7 lexicon used to fire on the
// embedded "error" via raw substring, inflating severity and admitting the benign
// line to the reservoir. It must be treated exactly like RareBenignNotAdmitted.
TEST(ReservoirTest, RareBenignWithEmbeddedFailureSubstringNotAdmitted)
{
    auto rare{make_event("Writing tsc-error-report.json", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Writing tsc-error-report.json"))
        << "F7 must not read 'error' inside a filename token as a failure cue";
}

// 2d structural_surprise (epic §5.1): a benign INFO template that severity⊗rarity
// scores 0 IS retained when it is reached only via a RECURRING low-probability
// transition off the dominant path. Distinct from RareBenignNotAdmitted: there the
// rare event is a single one-off (edge seen once → untrusted boundary artifact);
// here the off-path branch recurs (edge seen 3×), so it is a real alternate path.
TEST(ReservoirTest, StructuralSurpriseAdmitsRecurringOffPathBranch)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    // Dominant path A→B→C, 100×.
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha request received"));
        engine.ingest_event(make_event("beta verify token"));
        engine.ingest_event(make_event("gamma response sent"));
    }
    // Rare RECURRING off-path branch A→B→X→C, 3× — X is benign Info, lexically
    // clean, so its level/lexicon severity is 0. It is salient ONLY structurally:
    // B→X is a ~3% transition off the dominant B→C.
    for (int rep = 0; rep < 3; ++rep)
    {
        engine.ingest_event(make_event("alpha request received"));
        engine.ingest_event(make_event("beta verify token"));
        engine.ingest_event(make_event("took alternate cache path", insight::LogLevel::Info));
        engine.ingest_event(make_event("gamma response sent"));
    }
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                       std::chrono::seconds{60})};

    EXPECT_FALSE(top_k_has(doc, "took alternate cache path"))
        << "the branch is below top_k by frequency";
    ASSERT_TRUE(reservoir_has(doc, "took alternate cache path"))
        << "structural_surprise must retain a benign Info branch reached via a rare transition";
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_str == "took alternate cache path")
        {
            EXPECT_GT(entry.structural_surprise, 0U)
                << "retention must be attributed to structural_surprise, not severity";
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Info)
                << "the branch is benign Info — severity⊗rarity scores it 0";
            EXPECT_GT(entry.salience, 0U);
        }
}

// 2d-ii self-novelty (epic §5.1/§5.2): a benign INFO template that emerges LATE
// in the window (recurring, count >= 2) is retained even though severity AND
// structural_surprise score it 0. Isolation: the late template SELF-LOOPS, so its
// max incoming transition probability is 1.0 → structural_surprise 0; it is benign
// Info → severity 0. Only the self-novelty axis (late first-seen) can retain it.
TEST(ReservoirTest, NoveltyAdmitsLateEmergingBenignTemplate)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    // Steady bed present from the very start (first-seen ≈ 0 → no novelty).
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha steady event"));
        engine.ingest_event(make_event("beta steady event"));
        engine.ingest_event(make_event("gamma steady event"));
    }
    // A benign Info template that only STARTS near the end and recurs (self-loops):
    // late first-seen (≈0.98), count 5 ≥ 2, self-loop p=1.0 → structural_surprise 0.
    for (int rep = 0; rep < 5; ++rep)
        engine.ingest_event(make_event("cache warmer started", insight::LogLevel::Info));
    const auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                       std::chrono::seconds{60})};

    EXPECT_FALSE(top_k_has(doc, "cache warmer started")) << "the late template is below top_k";
    ASSERT_TRUE(reservoir_has(doc, "cache warmer started"))
        << "self-novelty must retain a benign template that emerged late in the window";
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_str == "cache warmer started")
        {
            EXPECT_GT(entry.novelty, 0U)
                << "retention must be attributed to novelty, not severity/structure";
            EXPECT_EQ(entry.structural_surprise, 0U)
                << "the self-loop makes it structurally expected — novelty is the only axis";
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Info);
            EXPECT_GT(entry.salience, 0U);
        }
}

// F8: the reservoir is part of the external JSON contract, so a serialised metalog
// document carries the rare-salient templates (and WHY they were kept) — without it
// a stored/transmitted document loses them and cross-process diffability breaks.
TEST(ReservoirTest, SerialisedToJsonWithAttribution)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, /*top_k=*/3, /*reservoir_size=*/8)};
    ASSERT_FALSE(doc.stats.reservoir.empty());

    const std::string json = meta::to_json(doc);
    auto parsed = glz::read_json<glz::generic>(json);
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << json;
    ASSERT_TRUE((*parsed)["stats"].contains("reservoir")) << json;
    auto& reservoir = (*parsed)["stats"]["reservoir"];
    ASSERT_TRUE(reservoir.is_array()) << json;
    ASSERT_FALSE(reservoir.get_array().empty()) << json;
    auto& entry = reservoir.get_array().front();
    // Self-describing: the per-axis bands + salience travel with the entry.
    EXPECT_TRUE(entry.contains("template_id")) << json;
    EXPECT_TRUE(entry.contains("salience")) << json;
    EXPECT_TRUE(entry.contains("structural_surprise")) << json;
    EXPECT_TRUE(entry.contains("novelty")) << json;
    EXPECT_TRUE(entry.contains("level")) << json; // the rare event is Error
}

// An empty reservoir is OMITTED from the JSON (restrictive emit) — streams with the
// reservoir disabled stay byte-identical to the pre-F8 contract.
TEST(ReservoirTest, EmptyReservoirOmittedFromJson)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 8, .reservoir_size = 0}};
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("steady"));
    const auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    const std::string json = meta::to_json(doc);
    auto parsed = glz::read_json<glz::generic>(json);
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed)["stats"].contains("reservoir")) << json;
}

// F8: compose() carries the reservoir (and its structural_surprise) instead of
// dropping rare-salient templates into the tail — so composed / pyramid-baseline
// documents are NOT blind to a lone fatal / off-path branch at long horizons.
TEST(ReservoirTest, SurvivesComposeWithStructuralSurprise)
{
    const auto t0{std::chrono::system_clock::now()};
    // lhs: a recurring off-path branch X (benign Info, structural_surprise > 0).
    meta::MetaLogEngine eng_l{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    eng_l.open_window(t0);
    for (int rep = 0; rep < 100; ++rep)
    {
        eng_l.ingest_event(make_event("alpha request received"));
        eng_l.ingest_event(make_event("beta verify token"));
        eng_l.ingest_event(make_event("gamma response sent"));
    }
    for (int rep = 0; rep < 3; ++rep)
    {
        eng_l.ingest_event(make_event("alpha request received"));
        eng_l.ingest_event(make_event("beta verify token"));
        eng_l.ingest_event(make_event("took alternate cache path", insight::LogLevel::Info));
        eng_l.ingest_event(make_event("gamma response sent"));
    }
    const auto lhs{eng_l.close_window(t0 + std::chrono::seconds{60})};
    ASSERT_TRUE(reservoir_has(lhs, "took alternate cache path"));
    std::uint32_t lhs_surprise{0};
    for (const auto& e : lhs.stats.reservoir)
        if (e.template_str == "took alternate cache path")
            lhs_surprise = e.structural_surprise;
    ASSERT_GT(lhs_surprise, 0U);

    // rhs: a plain benign bed — no salient templates of its own.
    meta::MetaLogEngine eng_r{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    eng_r.open_window(t0);
    for (int rep = 0; rep < 100; ++rep)
    {
        eng_r.ingest_event(make_event("alpha request received"));
        eng_r.ingest_event(make_event("beta verify token"));
        eng_r.ingest_event(make_event("gamma response sent"));
    }
    const auto rhs{eng_r.close_window(t0 + std::chrono::seconds{60})};

    const auto composed{meta::compose(lhs, rhs)};
    EXPECT_FALSE(top_k_has(composed, "took alternate cache path"))
        << "the branch is still below top_k after merge";
    ASSERT_TRUE(reservoir_has(composed, "took alternate cache path"))
        << "F8: compose() must carry the rare-salient template, not drop it to the tail";
    for (const auto& e : composed.stats.reservoir)
        if (e.template_str == "took alternate cache path")
        {
            EXPECT_GT(e.structural_surprise, 0U) << "structural_surprise must persist through compose";
            EXPECT_GT(e.salience, 0U);
        }
}

TEST(ReservoirTest, DisabledByDefault)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, 3, /*reservoir_size=*/0)};
    EXPECT_TRUE(doc.stats.reservoir.empty()) << "reservoir_size=0 → pure-frequency retention";
}

TEST(ReservoirTest, TailExcludesReservoirMembers)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto with_reservoir{run_with_rare_event(rare, 3, 8)};
    const auto without_reservoir{run_with_rare_event(rare, 3, 0)};
    // The admitted template moves out of the tail residual into the reservoir.
    EXPECT_EQ(with_reservoir.stats.tail_unique + with_reservoir.stats.reservoir.size(),
              without_reservoir.stats.tail_unique)
        << "tail must shrink by exactly the reservoir count (no double-counting)";
}

// F10: without a per-kind cap, the highest-salience failure CLASS monopolises the
// reservoir and crowds out a distinct (lower-salience) failure. The diversity cap
// bounds exemplars per (structural_role × dominant_level) kind to preserve coverage.
TEST(ReservoirTest, DiversityCapCoversDistinctKinds)
{
    const auto build_doc{[](std::size_t per_kind_cap)
                         {
                             meta::MetaLogEngine engine{meta::MetaLogConfig{
                                 .top_k_size = 3,
                                 .reservoir_size = 3,
                                 .reservoir_per_kind_cap = per_kind_cap,
                                 .emit_stability = false}};
                             engine.open_window(std::chrono::system_clock::time_point{});
                             for (int rep = 0; rep < 100; ++rep)
                             {
                                 engine.ingest_event(make_event("alpha steady event"));
                                 engine.ingest_event(make_event("beta steady event"));
                                 engine.ingest_event(make_event("gamma steady event"));
                             }
                             // Kind A: many high-salience Error variants of ONE failure class.
                             for (int n = 0; n < 9; ++n)
                                 engine.ingest_event(make_event(
                                     "test_query_" + std::to_string(n) + " FAILED",
                                     insight::LogLevel::Error));
                             // Kind B: a distinct, lower-salience failure (Warn).
                             engine.ingest_event(
                                 make_event("deprecated config option used", insight::LogLevel::Warn));
                             return engine.close_window(std::chrono::system_clock::time_point{} +
                                                        std::chrono::seconds{60});
                         }};
    const auto has_warn_kind{[](const meta::MetaLogDocument& doc)
                             {
                                 return std::ranges::any_of(doc.stats.reservoir,
                                                            [](const auto& e) {
                                                                return e.dominant_level ==
                                                                       insight::LogLevel::Warn;
                                                            });
                             }};
    const auto error_kind_count{[](const meta::MetaLogDocument& doc)
                                {
                                    return std::ranges::count_if(
                                        doc.stats.reservoir, [](const auto& e) {
                                            return e.dominant_level == insight::LogLevel::Error;
                                        });
                                }};

    const auto uncapped{build_doc(0)};
    EXPECT_EQ(error_kind_count(uncapped), 3)
        << "without a cap, M fills with the highest-salience failure class";
    EXPECT_FALSE(has_warn_kind(uncapped)) << "the distinct kind is crowded out";

    const auto capped{build_doc(2)};
    EXPECT_LE(error_kind_count(capped), 2) << "F10: the kind is capped to ≤2 exemplars";
    EXPECT_TRUE(has_warn_kind(capped))
        << "F10: the cap preserves a reservoir slot for the distinct failure kind";
}

// SPEC §3.7.2 normative MUST: salience admission is salience-ranked with a
// deterministic **tie-break by template_id**, so a given input under a given
// retention_profile yields a bit-identical reservoir. Two templates with equal
// salience (same level, same count, no other axis differentiating) admitted into
// a 1-slot reservoir: the smaller template_id wins.
TEST(ReservoirTest, TieBreakByTemplateIdAtEqualSalience)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 0,     // every template is a reservoir candidate
        .reservoir_size = 1, // exactly one slot — the tie must be broken
    }};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    // Two Error-level templates, count 1 each, no failure-lexicon words and no
    // structural surprise/novelty: salience comes purely from level → identical.
    engine.ingest_event(make_event("alpha", insight::LogLevel::Error));
    engine.ingest_event(make_event("beta", insight::LogLevel::Error));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};

    ASSERT_EQ(doc.stats.reservoir.size(), 1U);
    const auto tid_alpha{meta::MetaLogEngine::compute_template_id("alpha")};
    const auto tid_beta{meta::MetaLogEngine::compute_template_id("beta")};
    ASSERT_NE(tid_alpha, tid_beta);
    EXPECT_EQ(doc.stats.reservoir[0].template_id, std::min(tid_alpha, tid_beta))
        << "§3.7.2: at equal salience, the smaller template_id wins (got "
        << doc.stats.reservoir[0].template_id << "; min(tid_alpha,tid_beta)="
        << std::min(tid_alpha, tid_beta) << ")";
}

// ── §15 re-derivation coordinate ──────────────────────────────────────────────

TEST(ReDerivationCoordinate, AbsentWithoutSourceRef)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 8}};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};
    EXPECT_FALSE(doc.coordinate.has_value())
        << "no source_ref configured → no coordinate (the conservative default)";
}

TEST(ReDerivationCoordinate, StampsWindowEventTimeBounds)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft", .handle = "scenario#seed=7"};
    cfg.canonicalization_version = "canon-1";
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    const auto end{start + std::chrono::seconds(60)};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    const auto doc{engine.close_window(end)};

    ASSERT_TRUE(doc.coordinate.has_value());
    // §15.2 RAW coordinate: source_ref + bounds present, children absent.
    ASSERT_TRUE(doc.coordinate->source_ref.has_value());
    EXPECT_EQ(doc.coordinate->source_ref->resolver_kind, "logcraft");
    EXPECT_EQ(doc.coordinate->source_ref->handle, "scenario#seed=7");
    ASSERT_TRUE(doc.coordinate->bounds.has_value());
    // Bounds are the window's EVENT-TIME integer ticks, exactly (§15.3).
    EXPECT_EQ(doc.coordinate->bounds->start_tick,
              static_cast<std::uint64_t>(start.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->bounds->end_tick,
              static_cast<std::uint64_t>(end.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->canonicalization_version, "canon-1");
    EXPECT_FALSE(doc.coordinate->children.has_value()) << "a raw coordinate has no children";
}

TEST(ReDerivationCoordinate, SerialisesCoordinate)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft", .handle = "h"};
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    const std::string json{meta::to_json(engine.close_window(start + std::chrono::seconds(1)))};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << json;
    ASSERT_TRUE((*parsed).contains("coordinate")) << json;
    auto& coord{(*parsed)["coordinate"]};
    EXPECT_TRUE(coord.contains("source_ref")) << json;
    EXPECT_TRUE(coord.contains("bounds")) << json;
    EXPECT_TRUE(coord["bounds"].contains("start_tick")) << json;
    EXPECT_TRUE(coord["bounds"].contains("end_tick")) << json;
}

TEST(ReDerivationCoordinate, ReservoirEntryCarriesWithinWindowOrdinal)
{
    meta::MetaLogConfig cfg{.top_k_size = 2, .reservoir_size = 8};
    cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft", .handle = "h"};
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    // 20 benign events fill top_k; the rare error first appears at ordinal 20.
    for (int i = 0; i < 10; ++i)
    {
        engine.ingest_event(make_event("alpha"));
        engine.ingest_event(make_event("beta"));
    }
    engine.ingest_event(make_event("connection refused to db", insight::LogLevel::Error));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};

    bool found{false};
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_str == "connection refused to db")
        {
            found = true;
            ASSERT_TRUE(entry.within_window_ordinal.has_value())
                << "§15.4 sub-coordinate must be populated when a coordinate is configured";
            EXPECT_EQ(*entry.within_window_ordinal, 20U) << "first-seen ordinal after 20 benign events";
        }
    ASSERT_TRUE(found) << "the rare error must be retained in the reservoir";
}

TEST(ReDerivationCoordinate, ComposeCoordinateIsSetOfChildrenNotCoarseBound)
{
    const auto build{[](std::string handle, insight::Timestamp start)
                     {
                         meta::MetaLogConfig cfg{.top_k_size = 8};
                         cfg.source_ref =
                             meta::SourceRef{.resolver_kind = "logcraft", .handle = std::move(handle)};
                         meta::MetaLogEngine engine{cfg};
                         engine.open_window(start);
                         engine.ingest_event(make_event("alpha"));
                         return engine.close_window(start + std::chrono::seconds(30));
                     }};
    const auto t0{std::chrono::system_clock::now()};
    const auto lhs{build("scenario#seed=1", t0)};
    const auto rhs{build("scenario#seed=2", t0 + std::chrono::seconds(30))};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_TRUE(composed.coordinate.has_value());
    // §15.2 COMPOSED coordinate: source_ref + bounds ABSENT (no sentinel — §15.2
    // explicitly forbids them on composed); children present, addressing raw kids.
    EXPECT_FALSE(composed.coordinate->source_ref.has_value())
        << "a composed coordinate MUST NOT carry source_ref (§15.2)";
    EXPECT_FALSE(composed.coordinate->bounds.has_value())
        << "a composed coordinate MUST NOT carry bounds (§15.2) — children are authoritative";
    ASSERT_TRUE(composed.coordinate->children.has_value());
    ASSERT_EQ(composed.coordinate->children->size(), 2U) << "the set of the two raw children (§15.5)";
    // Each child is a RAW coordinate addressing the input — source_ref + bounds set.
    ASSERT_TRUE((*composed.coordinate->children)[0].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[0].source_ref->handle, "scenario#seed=1");
    ASSERT_TRUE((*composed.coordinate->children)[1].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[1].source_ref->handle, "scenario#seed=2");
}

// ── §2.4 processing identifiers + comparability gate ─────────────────────────

namespace
{
[[nodiscard]] meta::MetaLogDocument build_doc_with_identifiers(
    const std::optional<std::string>& canon, const std::optional<std::string>& retention)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.canonicalization_version = canon;
    cfg.retention_profile = retention;
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    return engine.close_window(start + std::chrono::seconds(60));
}
} // namespace

TEST(ProcessingIdentifiers, StampedFromConfigOnDocument)
{
    const auto doc{build_doc_with_identifiers("canon-1", "retention-A")};
    EXPECT_EQ(doc.canonicalization_version, "canon-1");
    EXPECT_EQ(doc.retention_profile, "retention-A");
}

TEST(ProcessingIdentifiers, ComposeCarriesWhenBothMatch)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto out{meta::compose(lhs, rhs)};
    EXPECT_EQ(out.canonicalization_version, "canon-1");
    EXPECT_EQ(out.retention_profile, "retention-A");
}

// §2.4: "When an input omits an identifier, the operation MAY proceed but the
// consumer SHOULD treat the result with caution." We proceed but OMIT the
// asymmetric identifier on the output rather than over-claim a contract.
TEST(ProcessingIdentifiers, ComposeAsymmetricProceedsButOmitsIdentifier)
{
    const auto lhs{build_doc_with_identifiers("canon-1", std::nullopt)};
    const auto rhs{build_doc_with_identifiers(std::nullopt, std::nullopt)};
    const auto out{meta::compose(lhs, rhs)};
    EXPECT_FALSE(out.canonicalization_version.has_value())
        << "asymmetric identifier must NOT be carried — over-claiming a contract is unsafe";
    EXPECT_FALSE(out.retention_profile.has_value());
}

TEST(ProcessingIdentifiers, ComposeMismatchedCanonicalizationVersionThrows)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-2", "retention-A")};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "§2.4 gate: canon mismatch MUST fail";
}

TEST(ProcessingIdentifiers, ComposeMismatchedRetentionProfileThrows)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-1", "retention-B")};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "§2.4 gate: retention profile mismatch MUST fail";
}

TEST(ProcessingIdentifiers, DiffMismatchedCanonicalizationVersionThrows)
{
    const auto previous{build_doc_with_identifiers("canon-1", std::nullopt)};
    const auto current{build_doc_with_identifiers("canon-2", std::nullopt)};
    EXPECT_THROW(meta::diff(previous, current), std::invalid_argument)
        << "§2.4 gate at §13: diff across mismatched canon MUST fail";
}

TEST(ProcessingIdentifiers, DiffMatchingIdentifiersSucceeds)
{
    const auto previous{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto current{build_doc_with_identifiers("canon-1", "retention-A")};
    EXPECT_NO_THROW((void)meta::diff(previous, current));
}

TEST(ProcessingIdentifiers, SerialisesAtDocumentRoot)
{
    const auto doc{build_doc_with_identifiers("canon-1", "retention-A")};
    const std::string json{meta::to_json(doc)};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_TRUE((*parsed).contains("canonicalization_version")) << json;
    EXPECT_TRUE((*parsed).contains("retention_profile")) << json;
}

// ── §3.5 / §12.1 param_histograms compose-carry ───────────────────────────────

namespace
{
[[nodiscard]] meta::MetaLogDocument
make_doc_with_histogram(std::string_view template_id, std::uint32_t param_index,
                        std::unordered_map<std::string, std::uint64_t> values,
                        std::uint64_t total, std::uint64_t approximate_cardinality,
                        std::uint64_t lines_observed)
{
    meta::MetaLogDocument doc;
    doc.window.lines_observed = lines_observed;
    doc.stats.unique_templates = 1;
    doc.stats.top_k_size = 8;
    meta::TopKEntry entry;
    entry.template_id = std::string{template_id};
    entry.count = total;
    entry.frequency = lines_observed > 0 ? static_cast<double>(total) /
                                               static_cast<double>(lines_observed)
                                         : 0.0;
    meta::FieldHistogram fh;
    fh.param_index = param_index;
    fh.value_counts = std::move(values);
    fh.total = total;
    fh.approximate_cardinality = approximate_cardinality;
    entry.field_histograms.push_back(std::move(fh));
    doc.stats.top_k.push_back(std::move(entry));
    return doc;
}
} // namespace

TEST(ParamHistogramsCompose, MergesValueCountsAndTotalForSharedSlot)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0,
                                           {{"/api/users", 800}, {"/health", 200}},
                                           /*total=*/1100, /*card=*/1847, /*lines=*/2000)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, {{"/api/users", 100}, {"/admin", 50}},
                                           /*total=*/200, /*card=*/50, /*lines=*/500)};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    const auto& fh{composed.stats.top_k[0].field_histograms[0]};
    EXPECT_EQ(fh.param_index, 0U);
    EXPECT_EQ(fh.value_counts.at("/api/users"), 900U) << "overlapping key sums counts";
    EXPECT_EQ(fh.value_counts.at("/health"), 200U) << "lhs-only key carried";
    EXPECT_EQ(fh.value_counts.at("/admin"), 50U) << "rhs-only key carried";
    EXPECT_EQ(fh.total, 1300U) << "§12.1: total = lhs.total + rhs.total";
    EXPECT_GT(fh.entropy_bits, 0.0) << "entropy recomputed from merged counts";
}

// §12.1 fallback: HLL sketches are not in the document, so the composer cannot
// union them — use max(A, B) as a conservative lower-bound estimate.
TEST(ParamHistogramsCompose, ApproximateCardinalityIsMaxAcrossInputs)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0, {{"x", 10}}, 10, /*card=*/1847, 100)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, {{"x", 5}}, 5, /*card=*/50, 50)};
    const auto composed{meta::compose(lhs, rhs)};
    EXPECT_EQ(composed.stats.top_k[0].field_histograms[0].approximate_cardinality, 1847U);
}

// §12.1: a slot present in only ONE input MAY be carried unchanged. We carry
// (preserves more information than omitting) so the composed FieldDrift/FieldShift
// can still see the asymmetric distribution.
TEST(ParamHistogramsCompose, CarriesOneSidedHistogramUnchanged)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0, {{"x", 10}}, 10, /*card=*/100, 100)};
    meta::MetaLogDocument rhs;
    rhs.window.lines_observed = 100;
    rhs.stats.unique_templates = 1;
    rhs.stats.top_k_size = 8;
    meta::TopKEntry rhs_entry;
    rhs_entry.template_id = "h:abc";
    rhs_entry.count = 50;
    rhs.stats.top_k.push_back(std::move(rhs_entry)); // template present, NO histogram

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    const auto& fh{composed.stats.top_k[0].field_histograms[0]};
    EXPECT_EQ(fh.value_counts.at("x"), 10U) << "lhs-only slot carried unchanged";
    EXPECT_EQ(fh.total, 10U) << "total reflects only the input that had the histogram";
}

// §12.1: merged value_counts is truncated to the producer's max_param_histograms
// cap, keeping the top-N by count (deterministic tie-break by key).
TEST(ParamHistogramsCompose, TruncatesMergedValueCountsToCap)
{
    constexpr std::size_t kCap{meta::MetaLogConfig::kDefaultMaxHistogramValues}; // 64
    std::unordered_map<std::string, std::uint64_t> a;
    std::unordered_map<std::string, std::uint64_t> b;
    // 50 lhs + 50 rhs → 100 distinct (disjoint key spaces, no overlap), exceeds cap.
    // Counts ascending so a stable top-N has the highest indices win.
    for (std::uint64_t i = 0; i < 50; ++i)
        a["a" + std::to_string(i)] = i + 1; // 1..50
    for (std::uint64_t i = 0; i < 50; ++i)
        b["b" + std::to_string(i)] = 100 + i; // 100..149
    const auto lhs{make_doc_with_histogram("h:abc", 0, std::move(a), 1275, 0, 5000)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, std::move(b), 6225, 0, 5000)};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    EXPECT_EQ(composed.stats.top_k[0].field_histograms[0].value_counts.size(), kCap)
        << "merged value_counts MUST be truncated to the cap (top-N by count, §12.1)";
}

// When neither input emits histograms (a degenerate case), the composed entry's
// field_histograms stays empty — no spurious empty histogram introduced.
TEST(ParamHistogramsCompose, NoHistogramsWhenInputsHaveNone)
{
    meta::MetaLogDocument lhs;
    lhs.window.lines_observed = 100;
    lhs.stats.top_k_size = 8;
    meta::TopKEntry lhs_e;
    lhs_e.template_id = "h:abc";
    lhs_e.count = 50;
    lhs.stats.top_k.push_back(std::move(lhs_e));
    const meta::MetaLogDocument rhs{lhs}; // same shape; no histograms anywhere

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    EXPECT_TRUE(composed.stats.top_k[0].field_histograms.empty());
}

// Wire-level XOR (§15.2 encoding note): a composed coordinate's JSON has `children`
// and MUST NOT carry `source_ref` or `bounds` (no sentinel emission).
TEST(ReDerivationCoordinate, ComposedSerialisesAsChildrenOnlyXOR)
{
    const auto build{[](std::string handle, insight::Timestamp start)
                     {
                         meta::MetaLogConfig cfg{.top_k_size = 8};
                         cfg.source_ref =
                             meta::SourceRef{.resolver_kind = "logcraft", .handle = std::move(handle)};
                         meta::MetaLogEngine engine{cfg};
                         engine.open_window(start);
                         engine.ingest_event(make_event("alpha"));
                         return engine.close_window(start + std::chrono::seconds(30));
                     }};
    const auto t0{std::chrono::system_clock::now()};
    const auto composed{
        meta::compose(build("seed=1", t0), build("seed=2", t0 + std::chrono::seconds(30)))};
    const std::string json{meta::to_json(composed)};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << "serialised composed doc did not parse: " << json;
    ASSERT_TRUE((*parsed).contains("coordinate")) << json;
    auto& coord{(*parsed)["coordinate"]};
    EXPECT_TRUE(coord.contains("children")) << "composed coordinate must carry children\n" << json;
    EXPECT_FALSE(coord.contains("source_ref"))
        << "§15.2: composed coordinate MUST NOT carry source_ref\n"
        << json;
    EXPECT_FALSE(coord.contains("bounds"))
        << "§15.2: composed coordinate MUST NOT carry bounds (no sentinel)\n"
        << json;
}

// ── F5.4 standing gate: full-document cross-machine bit-identity golden ────────
// The permanent determinism fixture (alongside S15Conformance). A fixed two-window
// scenario drives every F5 float path — entropy, KL/JS/stability, branching
// entropy, per-param histograms, HLL approximate_cardinality — and the SHA-256 of
// the serialised documents is FROZEN. Any architecture/compiler MUST reproduce the
// exact bytes; a mismatch is a cross-machine determinism regression. Re-derive the
// golden ONLY for an intentional contract change — and it must then hold across the
// cross-arch CI matrix (.github/workflows/determinism.yml; determinism_model.md).
TEST(DeterminismGate, FullDocumentByteIdentityGolden)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 3; // emit_stability defaults true
    meta::MetaLogEngine engine{cfg};
    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    const auto ingest_window = [&engine](int err_modulus)
    {
        for (int i = 0; i < 300; ++i)
        {
            const std::string path{"/api/r" + std::to_string(i % 7)};
            const bool err{i % err_modulus == 0};
            auto event{ParamEvent::make("GET <*> -> <*>", {path, err ? "500" : "200"},
                                        err ? insight::LogLevel::Error : insight::LogLevel::Info)};
            engine.ingest_event(event.event);
        }
        for (int i = 0; i < 60; ++i)
        {
            auto event{ParamEvent::make("worker <*> step <*>",
                                        {std::to_string(i % 11), std::to_string(i % 3)})};
            engine.ingest_event(event.event);
        }
        auto fatal{ParamEvent::make("disk <*> failed", {"sda1"}, insight::LogLevel::Fatal)};
        engine.ingest_event(fatal.event);
    };

    engine.open_window(t0);
    ingest_window(5); // window 1: ~20% errors
    const auto doc1{engine.close_window(t1)};
    engine.open_window(t1);
    ingest_window(2); // window 2: ~50% errors → divergence / stability vs window 1
    const auto doc2{engine.close_window(t2)};

    const std::string combined{meta::to_json(doc1) + "\n" + meta::to_json(doc2)};
    const std::string digest{picosha2::hash256_hex_string(combined)};

    // Frozen 2026-05-31. The same value must hold on every compiler/architecture
    // (verified across the F5 gcc×clang×-O×-ffp-contract matrix; F5.2 proved the
    // full document is byte-identical across all 12 builds).
    constexpr std::string_view kGolden{
        "798463355d66ec7a42a455118dd2cf530f9e1b56ebd3eef37a7814c640a4919f"};
    EXPECT_EQ(digest, kGolden)
        << "MetaLog document determinism golden mismatch — a cross-machine bit-identity "
           "regression, OR an intentional contract change needing the golden re-derived "
           "(and re-verified across the cross-arch CI matrix).\nDOC:\n"
        << combined;
}

} // namespace

// NOLINTEND
