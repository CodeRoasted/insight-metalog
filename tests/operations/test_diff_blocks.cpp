
// invariant: field_histogram_deltas carries a per-param JS divergence when histograms are enabled,
// and tail_delta is populated only when BOTH documents carry a tail summary.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

namespace
{
    // note: one template, a Bernoulli status distribution, two tracked params.
    meta::MetaLogDocument make_status_doc(insight::Timestamp start, insight::Timestamp end,
                                          std::size_t count_200, std::size_t count_500)
    {
        meta::MetaLogConfig cfg;
        cfg.max_param_histograms = 2;
        meta::MetaLogEngine eng{cfg};
        eng.open_window(start);
        // invariant: slot 0 is a constant path and slot 1 the varying status code, so slot 1 is the
        // one whose distribution diverges between windows.
        auto ev_200 = ParamEvent::make("GET <*> -> <*>", {"/api/users", "200"});
        auto ev_500 = ParamEvent::make("GET <*> -> <*>", {"/api/users", "500"});
        for (std::size_t i = 0; i < count_200; ++i)
            eng.ingest_event(ev_200.event);
        for (std::size_t i = 0; i < count_500; ++i)
            eng.ingest_event(ev_500.event);
        return eng.close_window(end);
    }
} // namespace

// invariant: with histograms disabled the default config produces no field_histogram_deltas, and
// diff() must neither crash nor invent one.
TEST(FieldHistogramDiffTest, EmptyWhenHistogramsDisabled)
{
    const auto t0 = insight::Timestamp{} + std::chrono::hours{1};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    meta::MetaLogEngine eng;
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

// invariant: a normal window against a degraded one must produce a non-zero JS divergence for the
// status-code slot.
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

    // note: slot 1 must show the largest shift of the two.
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

// invariant: identical value_counts and totals make the smoothed distributions equal, so the
// divergence is exactly zero; the arm asserts a 0.01 bound rather than that equality.
TEST(FieldHistogramDiffTest, JSDivergenceNearZeroForSameDistribution)
{
    const auto t0 = insight::Timestamp{} + std::chrono::hours{3};
    const auto t1 = t0 + std::chrono::seconds{60};
    const auto t2 = t1 + std::chrono::seconds{60};

    auto prev = make_status_doc(t0, t1, /*200=*/80, /*500=*/20);
    auto curr = make_status_doc(t1, t2, /*200=*/80, /*500=*/20);

    auto d = meta::diff(prev, curr);

    for (const auto& fhd : d.field_histogram_deltas)
    {
        EXPECT_LT(fhd.js_divergence, 0.01)
            << "JS divergence must be near-zero for identical distributions "
               "(param_index="
            << fhd.param_index << ")";
    }
}
// invariant: tail_delta carries before, after and delta for all three tail-summary fields; a tail
// growing louder and more concentrated shows a positive max-rate delta and a negative entropy one.
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
    without_tail.window.lines_observed = 1000;

    EXPECT_FALSE(meta::diff(with_tail, without_tail).tail_delta.has_value())
        << "a one-sided tail is appearance/vanishing, not a tail delta";
    EXPECT_FALSE(meta::diff(without_tail, with_tail).tail_delta.has_value());
}

} // namespace
