#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// note: value_counts is an unordered_map, so emission must key-sort or replay bit-identity is lost.
TEST(FieldHistogramSerializationTest, ValueCountsEmittedKeySorted)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

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
    meta::MetaLogEngine engine;
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

    // note: a cap-derived ceiling, not the measured overhead, which is printed and never asserted.
    constexpr std::size_t kBytesPerValueEntryUpperBound{96};
    const std::size_t cap_bound{top_k_count * kMaxHist * kMaxValues *
                                kBytesPerValueEntryUpperBound};
    EXPECT_LE(overhead, cap_bound)
        << "param_histograms overhead must stay within the cap-derived bound. overhead=" << overhead
        << "B cap_bound=" << cap_bound << "B";
}

namespace
{
    // post: one closed window carrying all three capped blocks -- a reservoir entry, branching
    // transitions and the always-on cube.
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

// refs: F-SRC-metalog-spec:SPEC.md
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
        meta::MetaLogConfig{
            .top_k_size = 3, .reservoir_size = 0, .emit_stability = false, .top_branching_size = 0},
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

namespace
{
    meta::MetaLogDocument window_of_twenty_distinct_templates(std::size_t cap,
                                                              meta::TemplateRegistry& registry)
    {
        meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16,
                                                       .top_ngrams_size = 16,
                                                       .max_ngram_keys = cap,
                                                       .emit_stability = false}};
        const std::chrono::system_clock::time_point t0{};
        engine.open_window(t0);
        for (int i{0}; i < 20; ++i)
            engine.ingest_event(meta::test::make_event("t" + std::to_string(i)));
        auto doc{engine.close_window(t0 + std::chrono::seconds{60})};
        registry = engine.registry();
        return doc;
    }
} // namespace

// refs: F-SRC-metalog-spec:SPEC.md
TEST(DroppedNgramObservationsWireTest, ACappedWindowWritesTheRefusedObservationCount)
{
    meta::TemplateRegistry registry;
    const auto doc{window_of_twenty_distinct_templates(/*cap=*/4, registry)};
    const std::string json{meta::to_json(doc, registry)};

    ASSERT_TRUE(doc.behavior.has_value()) << json;
    EXPECT_NE(json.find("\"dropped_ngram_observations\":15"), std::string::npos)
        << "SPEC §4: a window whose accounting bound BOUND must report the refused observation "
           "count — 20 distinct templates form 19 bigrams, 4 fit under the cap, 15 are refused.\n"
        << json;
}

TEST(DroppedNgramObservationsWireTest, AnUncappedWindowOmitsTheKeyRatherThanWritingZero)
{
    meta::TemplateRegistry registry;
    const auto doc{
        window_of_twenty_distinct_templates(meta::MetaLogConfig{}.max_ngram_keys, registry)};
    const std::string json{meta::to_json(doc, registry)};

    ASSERT_TRUE(doc.behavior.has_value()) << "the block must be PRESENT, or the absence below is "
                                             "the block's, not the field's.\n"
                                          << json;
    ASSERT_FALSE(doc.behavior->top_ngrams.empty())
        << "the block must be POPULATED — an empty n-gram array is a different silence.\n"
        << json;
    ASSERT_NE(json.find("\"top_ngrams_size\":16"), std::string::npos)
        << "the emitted BYTES must carry the behavior block the assertion below is about.\n"
        << json;

    EXPECT_FALSE(doc.behavior->dropped_ngram_observations.has_value())
        << "at cap 4096 nothing can be refused, so the domain value must be absent — it reported "
        << doc.behavior->dropped_ngram_observations.value_or(0) << ".\n"
        << json;
    EXPECT_EQ(json.find("\"dropped_ngram_observations\""), std::string::npos)
        << "SPEC §4: OMITTED when zero — the key is never written, not written as 0. The schema's "
           "`minimum: 1` refuses a 0, and the omission is what keeps a never-binding producer "
           "byte-identical to one that has no bound at all.\n"
        << json;
}

} // namespace
