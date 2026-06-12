// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Wire serialisation: key-sorted value_counts, omit-empty discipline, bounded document overhead.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

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

    const auto build{
        [&](std::size_t max_param_histograms)
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
                const std::string tstr{"t" + std::to_string(tmpl) + " path=<*> code=<*>"};
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
    const std::size_t cap_bound{top_k_count * kMaxHist * kMaxValues *
                                kBytesPerValueEntryUpperBound};
    EXPECT_LE(overhead, cap_bound)
        << "param_histograms overhead must stay within the cap-derived bound. overhead=" << overhead
        << "B cap_bound=" << cap_bound << "B";
}

// ── FieldHistogramDiffTest ────────────────────────────────────────────────────

} // namespace

// NOLINTEND
