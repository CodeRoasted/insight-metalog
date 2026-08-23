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
    const std::string json{meta::to_json(doc, engine.registry())};

    ASSERT_NE(json.find("\"param_histograms\""), std::string::npos)
        << "param_histograms must be emitted when max_param_histograms > 0.\n"
        << json;
    const auto pos_a{json.find("aaa_alpha")};
    const auto pos_m{json.find("mmm_mango")};
    const auto pos_z{json.find("zzz_zebra")};
    ASSERT_NE(pos_a, std::string::npos) << json;
    ASSERT_NE(pos_m, std::string::npos) << json;
    ASSERT_NE(pos_z, std::string::npos) << json;
    EXPECT_LT(pos_a, pos_m) << "value_counts must serialise key-sorted.\n" << json;
    EXPECT_LT(pos_m, pos_z) << "value_counts must serialise key-sorted.\n" << json;

    // entropy_bits MUST NOT be emitted: it is a float and is losslessly derivable
    // from value_counts, so a consumer that wants it computes it. Every
    // emitted field is integer-TYPED — param_index, value_counts counts, total, and
    // the HLL approximate_cardinality (=3 distinct values here) — so no float lands
    // on the wire. Pin the exact shape (key-sorted value_counts).
    EXPECT_NE(json.find("\"param_histograms\":[{\"param_index\":0,\"value_counts\":"
                        "{\"aaa_alpha\":1,\"mmm_mango\":1,\"zzz_zebra\":1},\"total\":3,"
                        "\"approximate_cardinality\":3}]"),
              std::string::npos)
        << "param_histograms must serialise integer-only & key-sorted (no entropy_bits).\n"
        << json;

    EXPECT_EQ(meta::to_json(doc, engine.registry()), json)
        << "serialisation must be byte-identical on repeat.";
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
    const std::string json{meta::to_json(doc, engine.registry())};

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
            -> std::pair<meta::MetaLogDocument, meta::TemplateRegistry>
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
            auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
            return {std::move(doc), engine.registry()};
        }};

    const auto [doc_with, reg_with]{build(kMaxHist)};
    const auto [doc_without, reg_without]{build(0)};
    const std::string json_with{meta::to_json(doc_with, reg_with)};
    const std::string json_without{meta::to_json(doc_without, reg_without)};

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

// ── DeclaredCapSerializationTest ──────────────────────────────────────────────
//
// SPEC §8 clause 4 made the caps decidable from the document alone, and §4.2 made the
// ABSENCE of `behavior.branching_size` a positive assertion ("the producer declares no
// cap"). So for every variable-length block this producer caps, two things are pinned
// here: the cap is DECLARED beside the array it bounds, and it is declared ONLY there —
// a cap standing next to an absent array would price a term the document does not carry,
// and an absent cap next to a capped array is the false statement §4.2 describes.
// String-level assertions on purpose: the claim is about emitted BYTES.

namespace
{
    // One window carrying all three capped blocks: a rare error below a small top_k
    // (populates `stats.reservoir`), repeated transitions (populate `behavior.branching`),
    // and the always-on cube.
    meta::MetaLogDocument window_with_every_capped_block(meta::MetaLogConfig config,
                                                         meta::TemplateRegistry& registry)
    {
        meta::MetaLogEngine engine{config};
        const std::chrono::system_clock::time_point t0{};
        engine.open_window(t0);
        for (int rep{0}; rep < 100; ++rep)
        {
            engine.ingest_event(meta::test::make_event("alpha steady event"));
            engine.ingest_event(meta::test::make_event("beta steady event"));
            engine.ingest_event(meta::test::make_event("gamma steady event"));
        }
        engine.ingest_event(
            meta::test::make_event("connection refused to db", insight::LogLevel::Error));
        auto doc{engine.close_window(t0 + std::chrono::seconds{60})};
        registry = engine.registry();
        return doc;
    }
} // namespace

TEST(DeclaredCapSerializationTest, EveryEmittedCappedBlockDeclaresItsCapAndHonoursIt)
{
    constexpr std::size_t kReservoirCap{8};
    constexpr std::size_t kBranchingCap{4};
    meta::TemplateRegistry registry;
    const auto doc{
        window_with_every_capped_block(meta::MetaLogConfig{.top_k_size = 3,
                                                           .reservoir_size = kReservoirCap,
                                                           .emit_stability = false,
                                                           .top_branching_size = kBranchingCap},
                                       registry)};
    const std::string json{meta::to_json(doc, registry)};

    ASSERT_FALSE(doc.stats.reservoir.empty()) << "fixture must populate the reservoir.\n" << json;
    ASSERT_TRUE(doc.behavior.has_value()) << json;
    ASSERT_TRUE(doc.behavior->branching.has_value()) << json;
    ASSERT_FALSE(doc.behavior->branching->empty()) << "fixture must populate branching.\n" << json;
    ASSERT_TRUE(doc.has_cube) << "the cube is always-on.\n" << json;

    // Declared — with the value the producer actually applied.
    EXPECT_NE(json.find("\"reservoir_size\":" + std::to_string(kReservoirCap)), std::string::npos)
        << "stats.reservoir_size must be declared beside an emitted reservoir (SPEC §3.7).\n"
        << json;
    EXPECT_NE(json.find("\"branching_size\":" + std::to_string(kBranchingCap)), std::string::npos)
        << "behavior.branching_size must be declared beside emitted branching — its absence "
           "asserts NO cap (SPEC §4.2), and this producer caps.\n"
        << json;
    EXPECT_NE(json.find("\"cell_budget\":4096"), std::string::npos)
        << "cube.cell_budget must be declared beside emitted cells (SPEC §16.10).\n"
        << json;

    // …and honoured: SPEC §8 clause 4 — the array is truthfully bounded by the cap the
    // SAME document declares for it.
    ASSERT_TRUE(doc.stats.reservoir_size.has_value()) << json;
    EXPECT_LE(doc.stats.reservoir.size(), *doc.stats.reservoir_size)
        << "clause 4: reservoir holds " << doc.stats.reservoir.size() << " under a declared cap of "
        << *doc.stats.reservoir_size << ".\n"
        << json;
    ASSERT_TRUE(doc.behavior->branching_size.has_value()) << json;
    EXPECT_LE(doc.behavior->branching->size(), *doc.behavior->branching_size)
        << "clause 4: branching holds " << doc.behavior->branching->size()
        << " under a declared cap of " << *doc.behavior->branching_size << ".\n"
        << json;
    ASSERT_TRUE(doc.cube.cell_budget.has_value()) << json;
    EXPECT_LE(doc.cube.cells.size(), *doc.cube.cell_budget)
        << "clause 4: cube holds " << doc.cube.cells.size() << " cells under a declared budget of "
        << *doc.cube.cell_budget << ".\n"
        << json;
}

TEST(DeclaredCapSerializationTest, ABlockThatIsNotEmittedDeclaresNoCap)
{
    meta::TemplateRegistry registry;
    const auto doc{window_with_every_capped_block(
        meta::MetaLogConfig{.top_k_size = 3,
                            .reservoir_size = 0, // reservoir disabled
                            .emit_stability = false,
                            .top_branching_size = 0}, // branching disabled
        registry)};
    const std::string json{meta::to_json(doc, registry)};

    ASSERT_TRUE(doc.stats.reservoir.empty()) << json;
    ASSERT_TRUE(doc.behavior.has_value()) << "behavior still carries top_ngrams.\n" << json;
    EXPECT_FALSE(doc.behavior->branching.has_value()) << json;

    EXPECT_EQ(json.find("\"reservoir_size\""), std::string::npos)
        << "a cap beside an absent array prices a term the document does not carry.\n"
        << json;
    EXPECT_EQ(json.find("\"branching_size\""), std::string::npos)
        << "a cap beside an absent array prices a term the document does not carry.\n"
        << json;
}

// ── FieldHistogramDiffTest ────────────────────────────────────────────────────

} // namespace

// NOLINTEND
