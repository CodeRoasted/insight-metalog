// refs: DN-56.D11, F-SRC-metalog-spec:SPEC.md
// invariant: an order-2 and an order-3 behavior block describe different populations, so compose()
// and diff() never merge or difference them as one.
// invariant: two STAMPED documents at different orders are refused by the retention_profile gate,
// its n axis being the order.
// invariant: two UNSTAMPED ones reach the operation, which omits the block (compose) or the
// n-gram delta (diff) rather than fabricate one.
// note: one input stream at both orders, so any n-gram turnover across the pair is the order's.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT1{std::chrono::seconds{1700000060}};

enum class Stamp : std::uint8_t
{
    Derived,
    Absent,
};

// post: one closed window over a four-template cycle at the requested n-gram order, stamped with
// the derived retention_profile or carrying none.
[[nodiscard]] meta::MetaLogDocument close_at_order(std::size_t ngram_size, Stamp stamp)
{
    constexpr int kCycles{6};
    meta::MetaLogConfig cfg{.top_k_size = 8, .ngram_size = ngram_size, .emit_stability = false};
    if (stamp == Stamp::Derived)
        cfg.retention_profile = meta::retention_profile_name(cfg);
    meta::MetaLogEngine engine{cfg};
    engine.open_window(kT0);
    for (int cycle = 0; cycle < kCycles; ++cycle)
    {
        engine.ingest_event(make_event("order alpha step"));
        engine.ingest_event(make_event("order beta step"));
        engine.ingest_event(make_event("order gamma step"));
        engine.ingest_event(make_event("order delta step"));
    }
    return engine.close_window(kT1);
}

[[nodiscard]] std::string stamp_of(const meta::MetaLogDocument& doc)
{
    return doc.retention_profile.value_or("<unset>");
}

} // namespace

TEST(NgramOrderComparability, StampedDocumentsAtDifferentOrdersAreRefusedByComposeAndDiff)
{
    const auto order2{close_at_order(2, Stamp::Derived)};
    const auto order3{close_at_order(3, Stamp::Derived)};
    ASSERT_NE(order2.retention_profile, order3.retention_profile)
        << "the two orders derived ONE stamp, so the gate cannot see the order: "
        << stamp_of(order2);

    EXPECT_THROW((void)meta::compose(order2, order3), std::invalid_argument)
        << "compose across " << stamp_of(order2) << " and " << stamp_of(order3);
    EXPECT_THROW((void)meta::compose(order3, order2), std::invalid_argument)
        << "and the refusal is symmetric";
    EXPECT_THROW((void)meta::diff(order2, order3), std::invalid_argument)
        << "diff across " << stamp_of(order2) << " and " << stamp_of(order3)
        << " would report every bigram vanished and every trigram new";
    EXPECT_THROW((void)meta::diff(order3, order2), std::invalid_argument);
}

TEST(NgramOrderComparability, UnstampedComposeAcrossOrdersOmitsBehaviorInBothBracketings)
{
    const auto order2{close_at_order(2, Stamp::Absent)};
    const auto order3{close_at_order(3, Stamp::Absent)};
    ASSERT_TRUE(order2.behavior.has_value() && order3.behavior.has_value())
        << "both inputs must carry a behavior block, or the order check is never reached";
    ASSERT_EQ(order2.behavior->ngram_size, 2U);
    ASSERT_EQ(order3.behavior->ngram_size, 3U);

    const auto forward{meta::compose(order2, order3)};
    const auto backward{meta::compose(order3, order2)};
    EXPECT_FALSE(forward.behavior.has_value())
        << "compose(order 2, order 3) carried a behavior block at ngram_size "
        << (forward.behavior ? forward.behavior->ngram_size : 0U) << " holding "
        << (forward.behavior ? forward.behavior->top_ngrams.size() : 0U)
        << " n-grams; no merged population exists across two orders";
    EXPECT_FALSE(backward.behavior.has_value())
        << "compose(order 3, order 2) carried a behavior block at ngram_size "
        << (backward.behavior ? backward.behavior->ngram_size : 0U);
    EXPECT_EQ(forward.window.lines_observed,
              order2.window.lines_observed + order3.window.lines_observed)
        << "the omission is block-grained: the rest of the document still composes";
}

TEST(NgramOrderComparability, UnstampedComposeAtOneOrderCarriesThatOrder)
{
    const auto lhs{close_at_order(3, Stamp::Absent)};
    const auto rhs{close_at_order(3, Stamp::Absent)};
    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_TRUE(composed.behavior.has_value())
        << "two blocks at one order must compose into a block, or the omission is unconditional";
    EXPECT_EQ(composed.behavior->ngram_size, 3U);
}

TEST(NgramOrderComparability, UnstampedDiffAcrossOrdersCarriesNoNgramDelta)
{
    const auto order2{close_at_order(2, Stamp::Absent)};
    const auto order3{close_at_order(3, Stamp::Absent)};

    const auto same_order{meta::diff(order3, close_at_order(3, Stamp::Absent))};
    ASSERT_FALSE(same_order.ngram_delta.has_value())
        << "one input at one order must diff to no n-gram delta, or this file's input moves";

    for (const auto& [name, delta] :
         {std::pair{"diff(order 2, order 3)", meta::diff(order2, order3)},
          std::pair{"diff(order 3, order 2)", meta::diff(order3, order2)}})
        EXPECT_FALSE(delta.ngram_delta.has_value())
            << name << " reported an n-gram delta (new "
            << (delta.ngram_delta ? delta.ngram_delta->new_ngrams.size() : 0U) << ", vanished "
            << (delta.ngram_delta ? delta.ngram_delta->vanished_ngrams.size() : 0U)
            << ") over ONE input stream: the turnover is the order's, not the workload's";
}
