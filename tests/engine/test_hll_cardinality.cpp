#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// refs: F-SRC-metalog-spec:SPEC.md
TEST(HllCardinalityTest, ApproximateCardinalityIsNonZeroWhenHistogramsEnabled)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 1;
    cfg.max_histogram_values = 10;
    meta::MetaLogEngine eng{cfg};

    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};

    eng.open_window(t0);
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
    EXPECT_GE(fh.approximate_cardinality, 5u) << "HLL estimate too low for 50 distinct values";
}

TEST(HllCardinalityTest, ApproximateCardinalityIsZeroWhenHistogramsDisabled)
{
    meta::MetaLogEngine eng;
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

    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 1;
    cfg.max_histogram_values = 10;

    meta::MetaLogEngine eng{cfg};
    const auto t0 = insight::Timestamp{} + std::chrono::hours{10};
    const auto t1 = t0 + std::chrono::seconds{60};

    eng.open_window(t0);
    for (int i = 0; i < 500; ++i)
    {
        auto e = ParamEvent::make("GET <*>", {"uid_" + std::to_string(i)});
        eng.ingest_event(e.event);
    }
    auto doc = eng.close_window(t1);

    const auto& fh = doc.stats.top_k[0].field_histograms[0];

    EXPECT_LE(fh.value_counts.size(), 10u)
        << "value_counts table must be capped at max_histogram_values";

    EXPECT_GT(fh.approximate_cardinality, 50u)
        << "HLL must track cardinality beyond the value_counts cap. The exact value table "
           "saturates at max_histogram_values, so without the HLL a cardinality explosion "
           "is invisible: approximate_cardinality must keep counting past the cap.";
}

TEST(HllCardinalityTest, CardinalityDeltaPopulatedInDiff)
{

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
            if (fhd.cardinality_delta > 0)
                found_positive_delta = true;
        }
    }
    EXPECT_TRUE(found_positive_delta)
        << "FieldHistogramDelta::cardinality_delta must be positive when the second "
           "window contains more distinct param values than the first.";
}

} // namespace
