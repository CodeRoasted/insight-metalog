// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Unit tests for the v0.2.0 spec-conformant MetaLogEngine.

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>
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
    EXPECT_EQ(doc.coordinate->source_ref.resolver_kind, "logcraft");
    EXPECT_EQ(doc.coordinate->source_ref.handle, "scenario#seed=7");
    // Bounds are the window's EVENT-TIME integer ticks, exactly (§15.3).
    EXPECT_EQ(doc.coordinate->bounds.start_tick,
              static_cast<std::uint64_t>(start.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->bounds.end_tick,
              static_cast<std::uint64_t>(end.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->canonicalization_version, "canon-1");
    EXPECT_FALSE(doc.coordinate->children.has_value()) << "a leaf coordinate has no children";
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
    EXPECT_EQ(composed.coordinate->source_ref.resolver_kind, "composed")
        << "a composed coordinate resolves via children, not a single source";
    ASSERT_TRUE(composed.coordinate->children.has_value());
    ASSERT_EQ(composed.coordinate->children->size(), 2U) << "the set of the two raw children (§15.5)";
    EXPECT_EQ((*composed.coordinate->children)[0].source_ref.handle, "scenario#seed=1");
    EXPECT_EQ((*composed.coordinate->children)[1].source_ref.handle, "scenario#seed=2");
    // §15.5: never a coarse single [first,last] bound on the composed node.
    EXPECT_EQ(composed.coordinate->bounds.start_tick, 0U);
    EXPECT_EQ(composed.coordinate->bounds.end_tick, 0U);
}

} // namespace

// NOLINTEND
