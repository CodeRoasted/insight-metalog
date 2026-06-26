// NOLINTBEGIN
// W1 ordinal carrier (§4A.4 D-W1-2): the field-keyed binned histogram on TopKEntry, its
// accumulation at ingest over the schedule's log2 ladder, the deterministic field-name emit order,
// the same-as-param batch gate, and the metalog::diff ordinal_histogram_deltas pairing the eidos
// W1 distance reads. Events are constructed directly with scope-stable storage (field_name is a
// string literal; the ordinals span is valid through ingest, which copies what it keeps).

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::OrdinalObservation;
using insight::OrdinalSchedule;

constexpr std::int64_t kMs{1'000'000}; // ns per ms

// Ingest one latency_ms observation (value in ms) on a fixed template.
void ingest_latency_ms(meta::MetaLogEngine& engine, std::int64_t value_ms)
{
    const std::array<OrdinalObservation, 1> obs{
        {{.field_name = "latency_ms", .schedule = OrdinalSchedule::DurationLog2Ns, .value = value_ms * kMs}}};
    tok::CanonicalEvent ev;
    ev.template_str = "db query completed";
    ev.ordinals = obs; // span valid through the ingest call
    engine.ingest_event(ev);
}

meta::MetaLogConfig batch_config()
{
    return meta::MetaLogConfig{.max_param_histograms = 2};
}
} // namespace

// ── The ladder (exported, pure integer) ─────────────────────────────────────────

TEST(OrdinalHistogramTest, BinIndexIsLog2OctaveClampedToB)
{
    // bin = floor(log2(value)), clamped to [0, B-1]. B = 48 for the duration schedule.
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 0), 0u);
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 1), 0u);
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 2), 1u);
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 3), 1u);   // floor(log2 3)=1
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 1024), 10u);
    // A huge value clamps to B-1 = 47, never out of range.
    EXPECT_EQ(meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns,
                                      std::numeric_limits<std::int64_t>::max()),
              47u);
}

// ── Carrier accumulation ─────────────────────────────────────────────────────────

TEST(OrdinalHistogramTest, DisabledByDefaultNoOrdinalHistograms)
{
    meta::MetaLogEngine engine; // default: max_param_histograms = 0
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    ingest_latency_ms(engine, 100);
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    ASSERT_FALSE(doc.stats.top_k.empty());
    EXPECT_TRUE(doc.stats.top_k[0].ordinal_histograms.empty())
        << "Default config must emit no ordinal histograms (the batch-gate / zero-overhead guarantee).";
}

TEST(OrdinalHistogramTest, AccumulatesBinnedCountsOnTheRightTemplate)
{
    meta::MetaLogEngine engine{batch_config()};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    // 5 events at 100 ms (→ same bin), 3 at 100000 ms (a far-higher bin).
    for (int i = 0; i < 5; ++i)
        ingest_latency_ms(engine, 100);
    for (int i = 0; i < 3; ++i)
        ingest_latency_ms(engine, 100'000);
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    const auto& entry{doc.stats.top_k[0]};
    ASSERT_EQ(entry.ordinal_histograms.size(), 1u);
    const auto& hist{entry.ordinal_histograms[0]};
    EXPECT_EQ(hist.field_name, "latency_ms");
    EXPECT_EQ(hist.schedule_id, "dur-log2-ns-v1"); // the versioned comparability key
    EXPECT_EQ(hist.total, 8u);
    EXPECT_EQ(hist.counts.size(), 48u); // full uncapped ladder
    const auto low_bin{meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 100 * kMs)};
    const auto high_bin{meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 100'000 * kMs)};
    ASSERT_NE(low_bin, high_bin);
    EXPECT_EQ(hist.counts[low_bin], 5u);
    EXPECT_EQ(hist.counts[high_bin], 3u);
    // The ordinal stream is separate from the categorical param stream.
    EXPECT_TRUE(entry.field_histograms.empty());
}

TEST(OrdinalHistogramTest, MultipleFieldsEmittedInSortedOrder)
{
    meta::MetaLogEngine engine{batch_config()};
    auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    // One event carrying two declared ordinals; field-name emit order must be deterministic (sorted).
    const std::array<OrdinalObservation, 2> obs{
        {{.field_name = "response_bytes", .schedule = OrdinalSchedule::SizeLog2Bytes, .value = 4096},
         {.field_name = "latency_ms", .schedule = OrdinalSchedule::DurationLog2Ns, .value = 50 * kMs}}};
    tok::CanonicalEvent ev;
    ev.template_str = "served";
    ev.ordinals = obs;
    engine.ingest_event(ev);
    auto doc{engine.close_window(t0 + std::chrono::seconds(1))};

    ASSERT_FALSE(doc.stats.top_k.empty());
    const auto& hists{doc.stats.top_k[0].ordinal_histograms};
    ASSERT_EQ(hists.size(), 2u);
    EXPECT_EQ(hists[0].field_name, "latency_ms");    // 'l' < 'r' — sorted
    EXPECT_EQ(hists[1].field_name, "response_bytes");
    EXPECT_EQ(hists[1].schedule_id, "size-log2-bytes-v1");
}

// ── Diff delta (what the eidos W1 distance reads) ───────────────────────────────

TEST(OrdinalHistogramTest, DiffPairsOrdinalHistogramsBothSides)
{
    auto build = [](std::int64_t value_ms)
    {
        meta::MetaLogEngine engine{batch_config()};
        auto t0{std::chrono::system_clock::now()};
        engine.open_window(t0);
        for (int i = 0; i < 10; ++i)
            ingest_latency_ms(engine, value_ms);
        return engine.close_window(t0 + std::chrono::seconds(1));
    };
    const auto baseline{build(100)};      // fast
    const auto changed{build(100'000)};   // slow (≈10 octaves up)

    const auto diff{meta::diff(baseline, changed)};
    ASSERT_EQ(diff.ordinal_histogram_deltas.size(), 1u);
    const auto& delta{diff.ordinal_histogram_deltas[0]};
    EXPECT_EQ(delta.field_name, "latency_ms");
    EXPECT_EQ(delta.previous_schedule_id, "dur-log2-ns-v1");
    EXPECT_EQ(delta.current_schedule_id, "dur-log2-ns-v1"); // matches → eidos will compute W1
    EXPECT_EQ(delta.previous_total, 10u);
    EXPECT_EQ(delta.current_total, 10u);
    ASSERT_EQ(delta.previous_counts.size(), 48u);
    ASSERT_EQ(delta.current_counts.size(), 48u);
    // The mass sits in different bins on each side (the drift the EMD will measure).
    const auto prev_bin{meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 100 * kMs)};
    const auto curr_bin{meta::ordinal_bin_index(OrdinalSchedule::DurationLog2Ns, 100'000 * kMs)};
    EXPECT_EQ(delta.previous_counts[prev_bin], 10u);
    EXPECT_EQ(delta.current_counts[curr_bin], 10u);
}

// NOLINTEND
