// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// BehaviorBlock: n-gram emission, conditional probabilities, bounded keys (MetaLogEngine close_window).

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

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

} // namespace

// NOLINTEND
