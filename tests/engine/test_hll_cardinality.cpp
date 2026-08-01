// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// HLL approximate cardinality per (template, param): population, capped-table behaviour, diff
// delta.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// ── HyperLogLog cardinality tests ────────────────────────────────────────────
//
// Verify that FieldHistogram::approximate_cardinality is populated by HLL
// and that FieldHistogramDelta::cardinality_delta captures the growth.
//
// The point of the field: approximate_cardinality is the UNCAPPED distinct-value
// count, so it stays right exactly where the capped value_counts table goes blind.

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
        << "HLL must track cardinality beyond the value_counts cap. The exact value table "
           "saturates at max_histogram_values, so without the HLL a cardinality explosion "
           "is invisible: approximate_cardinality must keep counting past the cap.";
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
