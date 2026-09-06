#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

namespace
{
    // pre: top_k is below the count of steady templates, so `rare` ranks last by frequency.
    // post: the closed document of one window holding four steady Info templates and `rare`.
    meta::MetaLogDocument run_with_rare_event(const tok::CanonicalEvent& rare, std::size_t top_k,
                                              std::size_t reservoir_size,
                                              meta::TemplateRegistry* out_registry = nullptr)
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
        engine.ingest_event(rare);
        auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                     std::chrono::seconds{60})};
        if (out_registry != nullptr)
            *out_registry =
                // refs: SRC-D-TIR-5
                engine.registry();
        return doc;
    }

    // post: whether the reservoir holds `tmpl`, by the content-derived id the masker assigns.
    // invariant: the masker is a pure function, so the expected id is recomputed from the string
    // and no registry is needed.
    // refs: SRC-D-TIR-5
    [[nodiscard]] bool reservoir_has(const meta::MetaLogDocument& doc, std::string_view tmpl)
    {
        const auto id{insight::template_id_of(tmpl)};
        return std::ranges::any_of(doc.stats.reservoir,
                                   [&](const auto& entry) { return entry.template_id == id; });
    }
    [[nodiscard]] bool top_k_has(const meta::MetaLogDocument& doc, std::string_view tmpl)
    {
        const auto id{insight::template_id_of(tmpl)};
        return std::ranges::any_of(doc.stats.top_k,
                                   [&](const auto& entry) { return entry.template_id == id; });
    }
    // post: whether this top_k or reservoir entry is `tmpl`, by content-derived id.
    [[nodiscard]] bool entry_is(const auto& entry, std::string_view tmpl)
    {
        return entry.template_id == insight::template_id_of(tmpl);
    }
} // namespace

TEST(ReservoirTest, RareErrorAdmittedBelowTopK)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, /*top_k=*/3, /*reservoir_size=*/8)};

    EXPECT_FALSE(top_k_has(doc, "connection refused to db"))
        << "the rare error is below top_k by frequency";
    ASSERT_TRUE(reservoir_has(doc, "connection refused to db"))
        << "a rare severe event must survive in the salience reservoir, not the tail";
    for (const auto& entry : doc.stats.reservoir)
        if (entry_is(entry, "connection refused to db"))
        {
            EXPECT_GT(entry.salience, 0U);
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Error);
        }
}

TEST(ReservoirTest, RareErrorNotRetainedWithoutReservoir)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, /*top_k=*/3, /*reservoir_size=*/0)};

    EXPECT_TRUE(doc.stats.reservoir.empty()) << "reservoir off → no salience retention path";
    EXPECT_FALSE(top_k_has(doc, "connection refused to db"))
        << "the rare error is below top_k by frequency (one occurrence vs the steady benign 100s)";
    EXPECT_FALSE(reservoir_has(doc, "connection refused to db"))
        << "with the reservoir off the rare severe event is tail dust — retained nowhere (the F1 "
           "recall=0 baseline the salience reservoir flips to 1)";
}

TEST(ReservoirTest, RareErrorRetainedAtGenerousTopKWithoutReservoir)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    const auto doc{run_with_rare_event(rare, /*top_k=*/16, /*reservoir_size=*/0)};
    EXPECT_TRUE(top_k_has(doc, "connection refused to db"))
        << "a top_k budget exceeding cardinality retains the rare event by frequency alone";
    EXPECT_TRUE(doc.stats.reservoir.empty()) << "no reservoir was configured";
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
    // assert: a rare Info line with no failure signal scores 0 -- benign rarity is chaff.
    auto rare{make_event("Downloading cache shard chunk", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Downloading cache shard chunk"))
        << "rarity must never gate a benign template into the reservoir";
}

// refs: SRC-D-PROV-1
TEST(ReservoirTest, RareBenignWithEmbeddedFailureSubstringNotAdmitted)
{
    auto rare{make_event("Writing tsc-error-report.json", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Writing tsc-error-report.json"))
        << "the lexicon must not read 'error' inside a filename token as a failure cue";
}

// assert: the off-path branch RECURS here, so it is a real alternate path rather than the single
// one-off of the benign-rarity arm above.
TEST(ReservoirTest, StructuralSurpriseAdmitsRecurringOffPathBranch)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha request received"));
        engine.ingest_event(make_event("beta verify token"));
        engine.ingest_event(make_event("gamma response sent"));
    }
    for (int rep = 0; rep < 3; ++rep)
    {
        engine.ingest_event(make_event("alpha request received"));
        engine.ingest_event(make_event("beta verify token"));
        engine.ingest_event(make_event("took alternate cache path", insight::LogLevel::Info));
        engine.ingest_event(make_event("gamma response sent"));
    }
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};

    EXPECT_FALSE(top_k_has(doc, "took alternate cache path"))
        << "the branch is below top_k by frequency";
    ASSERT_TRUE(reservoir_has(doc, "took alternate cache path"))
        << "structural_surprise must retain a benign Info branch reached via a rare transition";
    for (const auto& entry : doc.stats.reservoir)
        if (entry_is(entry, "took alternate cache path"))
        {
            EXPECT_GT(entry.structural_surprise, 0U)
                << "retention must be attributed to structural_surprise, not severity";
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Info)
                << "the branch is benign Info — severity⊗rarity scores it 0";
            EXPECT_GT(entry.salience, 0U);
        }
}

// assert: the late template SELF-LOOPS, so its incoming transition probability is 1 and structural
// surprise is 0; only self-novelty can retain it.
TEST(ReservoirTest, NoveltyAdmitsLateEmergingBenignTemplate)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha steady event"));
        engine.ingest_event(make_event("beta steady event"));
        engine.ingest_event(make_event("gamma steady event"));
    }
    // assert: it starts near the end and recurs, so first-seen is late and the count is >= 2.
    for (int rep = 0; rep < 5; ++rep)
        engine.ingest_event(make_event("cache warmer started", insight::LogLevel::Info));
    const auto doc{
        engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};

    EXPECT_FALSE(top_k_has(doc, "cache warmer started")) << "the late template is below top_k";
    ASSERT_TRUE(reservoir_has(doc, "cache warmer started"))
        << "self-novelty must retain a benign template that emerged late in the window";
    for (const auto& entry : doc.stats.reservoir)
        if (entry_is(entry, "cache warmer started"))
        {
            EXPECT_GT(entry.novelty, 0U)
                << "retention must be attributed to novelty, not severity/structure";
            EXPECT_EQ(entry.structural_surprise, 0U)
                << "the self-loop makes it structurally expected — novelty is the only axis";
            EXPECT_EQ(entry.dominant_level, insight::LogLevel::Info);
            EXPECT_GT(entry.salience, 0U);
        }
}

TEST(ReservoirTest, SerialisedToJsonWithAttribution)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    meta::TemplateRegistry registry;
    const auto doc{run_with_rare_event(rare, /*top_k=*/3, /*reservoir_size=*/8, &registry)};
    ASSERT_FALSE(doc.stats.reservoir.empty());

    const std::string json = meta::to_json(doc, registry);
    auto parsed = glz::read_json<glz::generic>(json);
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << json;
    ASSERT_TRUE((*parsed)["stats"].contains("reservoir")) << json;
    auto& reservoir = (*parsed)["stats"]["reservoir"];
    ASSERT_TRUE(reservoir.is_array()) << json;
    ASSERT_FALSE(reservoir.get_array().empty()) << json;
    auto& entry = reservoir.get_array().front();
    EXPECT_TRUE(entry.contains("template_id")) << json;
    EXPECT_TRUE(entry.contains("salience")) << json;
    EXPECT_TRUE(entry.contains("structural_surprise")) << json;
    EXPECT_TRUE(entry.contains("novelty")) << json;
    EXPECT_TRUE(entry.contains("level")) << json;
}

TEST(ReservoirTest, EmptyReservoirOmittedFromJson)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 8, .reservoir_size = 0}};
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
        engine.ingest_event(make_event("steady"));
    const auto doc{engine.close_window(t0 + std::chrono::seconds(1))};
    const std::string json = meta::to_json(doc, engine.registry());
    auto parsed = glz::read_json<glz::generic>(json);
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed)["stats"].contains("reservoir")) << json;
}

// refs: DN-56.D2
TEST(ReservoirTest, SurvivesComposeWithStructuralSurprise)
{
    const auto t0{std::chrono::system_clock::now()};
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
        if (entry_is(e, "took alternate cache path"))
            lhs_surprise = e.structural_surprise;
    ASSERT_GT(lhs_surprise, 0U);

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
        << "compose() must carry the rare-salient template, not drop it to the tail";
    for (const auto& e : composed.stats.reservoir)
        if (entry_is(e, "took alternate cache path"))
        {
            EXPECT_GT(e.structural_surprise, 0U)
                << "structural_surprise must persist through compose";
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
    EXPECT_EQ(with_reservoir.stats.tail_unique + with_reservoir.stats.reservoir.size(),
              without_reservoir.stats.tail_unique)
        << "tail must shrink by exactly the reservoir count (no double-counting)";
}

// refs: SRC-D-RNK-2
TEST(ReservoirTest, DiversityCapCoversDistinctKinds)
{
    const auto build_doc{
        [](std::size_t per_kind_cap)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 3,
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
            for (int n = 0; n < 9; ++n)
                engine.ingest_event(make_event("test_query_" + std::to_string(n) + " FAILED",
                                               insight::LogLevel::Error));
            engine.ingest_event(
                make_event("deprecated config option used", insight::LogLevel::Warn));
            return engine.close_window(std::chrono::system_clock::time_point{} +
                                       std::chrono::seconds{60});
        }};
    const auto has_warn_kind{
        [](const meta::MetaLogDocument& doc)
        {
            return std::ranges::any_of(doc.stats.reservoir, [](const auto& e)
                                       { return e.dominant_level == insight::LogLevel::Warn; });
        }};
    const auto error_kind_count{
        [](const meta::MetaLogDocument& doc)
        {
            return std::ranges::count_if(doc.stats.reservoir, [](const auto& e)
                                         { return e.dominant_level == insight::LogLevel::Error; });
        }};

    const auto uncapped{build_doc(0)};
    EXPECT_EQ(error_kind_count(uncapped), 3)
        << "without a cap, M fills with the highest-salience failure class";
    EXPECT_FALSE(has_warn_kind(uncapped)) << "the distinct kind is crowded out";

    const auto capped{build_doc(2)};
    EXPECT_LE(error_kind_count(capped), 2) << "the kind is capped to ≤2 exemplars";
    EXPECT_TRUE(has_warn_kind(capped))
        << "the cap preserves a reservoir slot for the distinct failure kind";
}

// invariant: the reserve is admitted AHEAD of the general pool and EXEMPT from the per-kind cap, so
// non-failure salience cannot evict a real failure.
// refs: SRC-D-RNK-2
namespace
{
    constexpr std::array<std::string_view, 3> kSurpriseBranches{
        "took alternate cache path", "took fallback dns route", "took degraded retry queue"};

    // post: a window whose three surprise branches each out-score the rare Error failure on
    // salience, so only the reserve can retain the failure.
    // invariant: each branch takes about 1.4 % of the transitions out of its predecessor, inside
    // the strong-off-path band.
    // invariant: the per-kind cap is 0 here, so the reserve is the only variable.
    [[nodiscard]] meta::MetaLogDocument build_high_card_window(std::size_t error_reserve)
    {
        meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 3,
                                                       .reservoir_size = 3,
                                                       .reservoir_per_kind_cap = 0,
                                                       .reservoir_error_reserve = error_reserve,
                                                       .emit_stability = false}};
        engine.open_window(std::chrono::system_clock::time_point{});
        for (int rep = 0; rep < 200; ++rep)
        {
            engine.ingest_event(make_event("alpha request received"));
            engine.ingest_event(make_event("beta verify token"));
            engine.ingest_event(make_event("gamma response sent"));
        }
        for (const std::string_view branch : kSurpriseBranches)
            for (int rep = 0; rep < 3; ++rep)
            {
                engine.ingest_event(make_event("alpha request received"));
                engine.ingest_event(make_event("beta verify token"));
                engine.ingest_event(make_event(std::string{branch}, insight::LogLevel::Info));
                engine.ingest_event(make_event("gamma response sent"));
            }
        engine.ingest_event(make_event("connection refused to db", insight::LogLevel::Error));
        return engine.close_window(std::chrono::system_clock::time_point{} +
                                   std::chrono::seconds{60});
    }

    [[nodiscard]] std::size_t branches_retained(const meta::MetaLogDocument& doc)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            kSurpriseBranches, [&](std::string_view b) { return reservoir_has(doc, b); }));
    }
} // namespace

TEST(ReservoirTest, ErrorClassReserveRetainsFailureAgainstNonFailureStorm)
{
    const auto with_reserve{build_high_card_window(/*error_reserve=*/1)};
    ASSERT_TRUE(reservoir_has(with_reserve, "connection refused to db"))
        << "the error-class reserve must retain the rare real failure in a high-card window";
    EXPECT_FALSE(top_k_has(with_reserve, "connection refused to db"))
        << "the failure is below top_k by frequency — retention is the reserve's doing, not top_k";
    EXPECT_EQ(branches_retained(with_reserve), 2U)
        << "exactly one higher-salience non-failure branch yielded its slot to the reserved "
           "failure";

    // assert: the retained failure scores LESS than the branches that kept their slots, which is
    // what proves the reserve admitted it rather than salience order.
    std::uint32_t error_salience{0};
    std::uint32_t min_branch_salience{std::numeric_limits<std::uint32_t>::max()};
    for (const auto& e : with_reserve.stats.reservoir)
    {
        if (entry_is(e, "connection refused to db"))
            error_salience = e.salience;
        else
            min_branch_salience = std::min(min_branch_salience, e.salience);
    }
    EXPECT_LT(error_salience, min_branch_salience)
        << "the reserve admitted a LOWER-salience failure (" << error_salience
        << ") over higher-salience non-failure templates (min retained " << min_branch_salience
        << ") — that is the whole point of the reserve";

    const auto no_reserve{build_high_card_window(/*error_reserve=*/0)};
    EXPECT_FALSE(reservoir_has(no_reserve, "connection refused to db"))
        << "negative control broken: without the reserve, non-failure salience must evict the "
           "real failure — that eviction is the loss the reserve exists to close";
    EXPECT_EQ(branches_retained(no_reserve), 3U)
        << "without the reserve, all three higher-salience non-failure branches keep the slots";
}

// invariant: the per-kind cap governs the GENERAL pool only, so the reserve admits multiple
// distinct failures the cap would have kept to one.
// refs: SRC-D-RNK-2
TEST(ReservoirTest, ErrorClassReserveIsExemptFromPerKindCap)
{
    const auto build{
        [](std::size_t error_reserve)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 3,
                                                           .reservoir_size = 4,
                                                           .reservoir_per_kind_cap = 1,
                                                           .reservoir_error_reserve = error_reserve,
                                                           .emit_stability = false}};
            engine.open_window(std::chrono::system_clock::time_point{});
            for (int rep = 0; rep < 100; ++rep)
            {
                engine.ingest_event(make_event("alpha steady event"));
                engine.ingest_event(make_event("beta steady event"));
                engine.ingest_event(make_event("gamma steady event"));
            }
            engine.ingest_event(
                make_event("disk write failed on shard 1", insight::LogLevel::Error));
            engine.ingest_event(
                make_event("auth token rejected by peer", insight::LogLevel::Error));
            engine.ingest_event(make_event("query deadline exceeded", insight::LogLevel::Error));
            engine.ingest_event(make_event("replica fell out of quorum", insight::LogLevel::Error));
            return engine.close_window(std::chrono::system_clock::time_point{} +
                                       std::chrono::seconds{60});
        }};
    const auto error_count{
        [](const meta::MetaLogDocument& doc)
        {
            return std::ranges::count_if(doc.stats.reservoir, [](const auto& e)
                                         { return e.dominant_level == insight::LogLevel::Error; });
        }};

    const auto capped_only{build(/*error_reserve=*/0)};
    EXPECT_EQ(error_count(capped_only), 1)
        << "with no reserve, the per-kind cap=1 keeps only ONE of the four distinct failures";

    const auto with_reserve{build(/*error_reserve=*/4)};
    EXPECT_GT(error_count(with_reserve), 1)
        << "the reserve is EXEMPT from the per-kind cap — multiple distinct failures survive";
}

// invariant: a template is all-echoed only while EVERY event forming it is echoed source, so one
// runtime occurrence makes the bucket not all-echoed.
// refs: SRC-D-PROV-1
TEST(ReservoirTest, AllEchoedFailureTemplateNotAdmittedButRuntimeOccurrenceRescues)
{
    const auto echoed_event{[](std::string_view tmpl)
                            {
                                // invariant: canon demotes an echoed line's level to Unknown and
                                // sets the flag; this mirrors that shape.
                                auto ev{make_event(tmpl, insight::LogLevel::Unknown)};
                                ev.echoed_source = true;
                                return ev;
                            }};

    // assert: the echoed run is ingested FIRST and self-loops, so novelty and structural surprise
    // are both 0 and the failure-cue tier is the only axis under test.
    {
        meta::MetaLogEngine engine{
            meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
        engine.open_window(std::chrono::system_clock::time_point{});
        for (int rep = 0; rep < 3; ++rep)
            engine.ingest_event(echoed_event("Download failed after 3 attempts"));
        for (int rep = 0; rep < 100; ++rep)
        {
            engine.ingest_event(make_event("alpha steady event"));
            engine.ingest_event(make_event("beta steady event"));
            engine.ingest_event(make_event("gamma steady event"));
        }
        const auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                           std::chrono::seconds{60})};
        EXPECT_FALSE(reservoir_has(doc, "Download failed after 3 attempts"))
            << "an all-echoed failure template (script source) must NOT be salient-as-failure — "
               "the level-blind failure-cue tier is skipped, so it scores 0 and is not admitted";
    }

    // assert: the SAME template seen once as a real runtime event makes the bucket not all-echoed,
    // so the failure-cue tier stands and it is admitted.
    {
        meta::MetaLogEngine engine{
            meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
        engine.open_window(std::chrono::system_clock::time_point{});
        engine.ingest_event(echoed_event("Download failed after 3 attempts"));
        engine.ingest_event(echoed_event("Download failed after 3 attempts"));
        engine.ingest_event(make_event("Download failed after 3 attempts"));
        for (int rep = 0; rep < 100; ++rep)
        {
            engine.ingest_event(make_event("alpha steady event"));
            engine.ingest_event(make_event("beta steady event"));
            engine.ingest_event(make_event("gamma steady event"));
        }
        const auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                           std::chrono::seconds{60})};
        EXPECT_TRUE(reservoir_has(doc, "Download failed after 3 attempts"))
            << "one real runtime occurrence makes all_echoed_source false → failure-cue tier "
               "stands → the template is admitted (the AND-reduction rescues a real failure)";
    }
}

// invariant: admission is salience-ranked with a deterministic tie-break by template_id, so one
// input under one retention profile yields a bit-identical reservoir.
// refs: ADR-31.D8
TEST(ReservoirTest, TieBreakByTemplateIdAtEqualSalience)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 0,
        .reservoir_size = 1,
    }};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    // assert: both templates are Error at count 1 with no lexicon word and no surprise or novelty,
    // so their salience is identical and only the tie-break can decide.
    engine.ingest_event(make_event("alpha", insight::LogLevel::Error));
    engine.ingest_event(make_event("beta", insight::LogLevel::Error));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};

    ASSERT_EQ(doc.stats.reservoir.size(), 1U);
    const auto tid_alpha{insight::template_id_of("alpha")};
    const auto tid_beta{insight::template_id_of("beta")};
    ASSERT_NE(tid_alpha, tid_beta);
    EXPECT_EQ(doc.stats.reservoir[0].template_id, std::min(tid_alpha, tid_beta))
        << "deterministic tie-break broken: at equal salience the smaller template_id must win, "
           "so the retained entry does not depend on ingestion order (got "
        << doc.stats.reservoir[0].template_id
        << "; min(tid_alpha,tid_beta)=" << std::min(tid_alpha, tid_beta) << ")";
}

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
    ASSERT_TRUE(doc.coordinate->source_ref.has_value());
    EXPECT_EQ(doc.coordinate->source_ref->resolver_kind, "logcraft");
    EXPECT_EQ(doc.coordinate->source_ref->handle, "scenario#seed=7");
    ASSERT_TRUE(doc.coordinate->bounds.has_value());
    EXPECT_EQ(doc.coordinate->bounds->start_tick,
              static_cast<std::uint64_t>(start.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->bounds->end_tick,
              static_cast<std::uint64_t>(end.time_since_epoch().count()));
    EXPECT_EQ(doc.coordinate->canonicalization_version, "canon-1");
    EXPECT_FALSE(doc.coordinate->children.has_value()) << "a raw coordinate has no children";
}

TEST(ReDerivationCoordinate, SerialisesCoordinate)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft", .handle = "h"};
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    const auto doc{engine.close_window(start + std::chrono::seconds(1))};
    const std::string json{meta::to_json(doc, engine.registry())};

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
    for (int i = 0; i < 10; ++i)
    {
        engine.ingest_event(make_event("alpha"));
        engine.ingest_event(make_event("beta"));
    }
    engine.ingest_event(make_event("connection refused to db", insight::LogLevel::Error));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};

    bool found{false};
    for (const auto& entry : doc.stats.reservoir)
        if (entry_is(entry, "connection refused to db"))
        {
            found = true;
            ASSERT_TRUE(entry.within_window_ordinal.has_value())
                << "the within-window ordinal must be populated whenever a source_ref "
                   "coordinate is configured — without it the entry is not re-derivable";
            EXPECT_EQ(*entry.within_window_ordinal, 20U)
                << "first-seen ordinal after 20 benign events";
        }
    ASSERT_TRUE(found) << "the rare error must be retained in the reservoir";
}

TEST(ReDerivationCoordinate, ComposeCoordinateIsSetOfChildrenNotCoarseBound)
{
    const auto build{[](std::string handle, insight::Timestamp start)
                     {
                         meta::MetaLogConfig cfg{.top_k_size = 8};
                         cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft",
                                                          .handle = std::move(handle)};
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
    // invariant: a composed coordinate carries children and MUST NOT carry source_ref or bounds --
    // a coarse first-to-last bound over-claims across the children's gaps and sources.
    EXPECT_FALSE(composed.coordinate->source_ref.has_value())
        << "a composed coordinate MUST NOT carry source_ref — it addresses no single source";
    EXPECT_FALSE(composed.coordinate->bounds.has_value())
        << "a composed coordinate MUST NOT carry bounds: a coarse [first,last] over-claims "
           "across the children's gaps, shards and sources — the children are authoritative";
    ASSERT_TRUE(composed.coordinate->children.has_value());
    ASSERT_EQ(composed.coordinate->children->size(), 2U)
        << "a composed coordinate carries exactly the set of its raw children, one per input";
    ASSERT_TRUE((*composed.coordinate->children)[0].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[0].source_ref->handle, "scenario#seed=1");
    ASSERT_TRUE((*composed.coordinate->children)[1].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[1].source_ref->handle, "scenario#seed=2");
}
TEST(ReDerivationCoordinate, ComposedSerialisesAsChildrenOnlyXOR)
{
    const auto build{[](std::string handle, insight::Timestamp start)
                     {
                         meta::MetaLogConfig cfg{.top_k_size = 8};
                         cfg.source_ref = meta::SourceRef{.resolver_kind = "logcraft",
                                                          .handle = std::move(handle)};
                         meta::MetaLogEngine engine{cfg};
                         engine.open_window(start);
                         engine.ingest_event(make_event("alpha"));
                         return engine.close_window(start + std::chrono::seconds(30));
                     }};
    const auto t0{std::chrono::system_clock::now()};
    const auto composed{
        meta::compose(build("seed=1", t0), build("seed=2", t0 + std::chrono::seconds(30)))};
    // assert: this arm asserts the coordinate encoding and not template strings, so an empty
    // registry is sufficient.
    const std::string json{meta::to_json(composed, meta::TemplateRegistry{})};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << "serialised composed doc did not parse: " << json;
    ASSERT_TRUE((*parsed).contains("coordinate")) << json;
    auto& coord{(*parsed)["coordinate"]};
    EXPECT_TRUE(coord.contains("children")) << "composed coordinate must carry children\n" << json;
    EXPECT_FALSE(coord.contains("source_ref"))
        << "a composed coordinate MUST NOT serialise source_ref — children only\n"
        << json;
    EXPECT_FALSE(coord.contains("bounds"))
        << "a composed coordinate MUST NOT serialise bounds — no sentinel stand-in for "
           "\"see children\"\n"
        << json;
}

} // namespace
