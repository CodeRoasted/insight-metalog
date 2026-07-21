// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Salience reservoir (Tier 2) + re-derivation coordinates: admission, caps, dedup, coordinate XOR
// (§15.2).

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

// ── Salience Reservoir (Tier 2) ──────────────────────────────────────────────
//
// A rare-but-severe template that falls below top_k by frequency must survive in
// the reservoir (admitted by salience) instead of collapsing into the tail.

namespace
{
    // Feed N frequent benign Info templates plus one rare event, with a small top_k
    // so the rare event is below it. Returns the closed document.
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
        engine.ingest_event(rare); // one occurrence — rank last by frequency
        auto doc{engine.close_window(std::chrono::system_clock::time_point{} +
                                     std::chrono::seconds{60})};
        if (out_registry != nullptr)
            *out_registry = engine.registry(); // D-TIR-5: caller serialises via the engine registry
        return doc;
    }

    // D-TIR-5 field-drop: entries carry only the content-derived TemplateId now (template_str moved
    // to the registry). A membership check computes the expected id from the string (the stateless
    // masker is a pure fn — same string → same id the engine assigned) and compares ids; no
    // registry needed.
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
    // A single entry's identity check (TopK or Reservoir), by content-derived id.
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

// The negative control of RareErrorAdmittedBelowTopK — re-homes 10's
// FatalNotRetainedWithoutReservoir (the F1 recall=0 baseline). With the reservoir OFF, a rare
// severe event below top_k by frequency is retained by NEITHER path: it collapses into the tail.
// The salience reservoir is exactly what flips this 0→1 (RareErrorAdmittedBelowTopK is the same
// input with the reservoir on).
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

// The frequency path itself — re-homes 10's FatalRetainedAtGenerousTopK_SignalExists. With a top_k
// budget exceeding the template cardinality the rare event sits in top_k directly (no reservoir
// needed) — the control proving the signal is present, distinct from the salience path.
TEST(ReservoirTest, RareErrorRetainedAtGenerousTopKWithoutReservoir)
{
    auto rare{make_event("connection refused to db", insight::LogLevel::Error)};
    // run_with_rare_event feeds 4 steady templates + the 1 rare = 5 distinct; top_k 16 ≫ 5.
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
    // A rare INFO line with no failure signal scores 0 — benign rarity is chaff,
    // never admitted (the cache-shard-481 case).
    auto rare{make_event("Downloading cache shard chunk", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Downloading cache shard chunk"))
        << "rarity must never gate a benign template into the reservoir";
}

// Token-awareness: a benign INFO line whose text merely CONTAINS a failure word
// inside a token (a filename) must score 0 — the lexicon used to fire on the
// embedded "error" via raw substring, inflating severity and admitting the benign
// line to the reservoir. It must be treated exactly like RareBenignNotAdmitted.
TEST(ReservoirTest, RareBenignWithEmbeddedFailureSubstringNotAdmitted)
{
    auto rare{make_event("Writing tsc-error-report.json", insight::LogLevel::Info)};
    const auto doc{run_with_rare_event(rare, 3, 8)};
    EXPECT_FALSE(reservoir_has(doc, "Writing tsc-error-report.json"))
        << "the lexicon must not read 'error' inside a filename token as a failure cue";
}

// 2d structural_surprise (epic §5.1): a benign INFO template that severity⊗rarity
// scores 0 IS retained when it is reached only via a RECURRING low-probability
// transition off the dominant path. Distinct from RareBenignNotAdmitted: there the
// rare event is a single one-off (edge seen once → untrusted boundary artifact);
// here the off-path branch recurs (edge seen 3×), so it is a real alternate path.
TEST(ReservoirTest, StructuralSurpriseAdmitsRecurringOffPathBranch)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    // Dominant path A→B→C, 100×.
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha request received"));
        engine.ingest_event(make_event("beta verify token"));
        engine.ingest_event(make_event("gamma response sent"));
    }
    // Rare RECURRING off-path branch A→B→X→C, 3× — X is benign Info, lexically
    // clean, so its level/lexicon severity is 0. It is salient ONLY structurally:
    // B→X is a ~3% transition off the dominant B→C.
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

// 2d-ii self-novelty (epic §5.1/§5.2): a benign INFO template that emerges LATE
// in the window (recurring, count >= 2) is retained even though severity AND
// structural_surprise score it 0. Isolation: the late template SELF-LOOPS, so its
// max incoming transition probability is 1.0 → structural_surprise 0; it is benign
// Info → severity 0. Only the self-novelty axis (late first-seen) can retain it.
TEST(ReservoirTest, NoveltyAdmitsLateEmergingBenignTemplate)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    // Steady bed present from the very start (first-seen ≈ 0 → no novelty).
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event("alpha steady event"));
        engine.ingest_event(make_event("beta steady event"));
        engine.ingest_event(make_event("gamma steady event"));
    }
    // A benign Info template that only STARTS near the end and recurs (self-loops):
    // late first-seen (≈0.98), count 5 ≥ 2, self-loop p=1.0 → structural_surprise 0.
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

// The reservoir is part of the external JSON contract, so a serialised metalog
// document carries the rare-salient templates (and WHY they were kept) — without it
// a stored/transmitted document loses them and cross-process diffability breaks.
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
    // Self-describing: the per-axis bands + salience travel with the entry.
    EXPECT_TRUE(entry.contains("template_id")) << json;
    EXPECT_TRUE(entry.contains("salience")) << json;
    EXPECT_TRUE(entry.contains("structural_surprise")) << json;
    EXPECT_TRUE(entry.contains("novelty")) << json;
    EXPECT_TRUE(entry.contains("level")) << json; // the rare event is Error
}

// An empty reservoir is OMITTED from the JSON (restrictive emit) — streams with the
// reservoir disabled stay byte-identical to the pre-reservoir contract.
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

// compose() carries the reservoir (and its structural_surprise) instead of
// dropping rare-salient templates into the tail — so composed / pyramid-baseline
// documents are NOT blind to a lone fatal / off-path branch at long horizons.
TEST(ReservoirTest, SurvivesComposeWithStructuralSurprise)
{
    const auto t0{std::chrono::system_clock::now()};
    // lhs: a recurring off-path branch X (benign Info, structural_surprise > 0).
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

    // rhs: a plain benign bed — no salient templates of its own.
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
    // The admitted template moves out of the tail residual into the reservoir.
    EXPECT_EQ(with_reservoir.stats.tail_unique + with_reservoir.stats.reservoir.size(),
              without_reservoir.stats.tail_unique)
        << "tail must shrink by exactly the reservoir count (no double-counting)";
}

// Without a per-kind cap, the highest-salience failure CLASS monopolises the
// reservoir and crowds out a distinct (lower-salience) failure. The diversity cap
// bounds exemplars per (structural_role × dominant_level) kind to preserve coverage.
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
            // Kind A: many high-salience Error variants of ONE failure class.
            for (int n = 0; n < 9; ++n)
                engine.ingest_event(make_event("test_query_" + std::to_string(n) + " FAILED",
                                               insight::LogLevel::Error));
            // Kind B: a distinct, lower-salience failure (Warn).
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

// ── D-RNK-2 (§5.2) — the error-class RETENTION RESERVE (the P5 recall fix) ──────
// The §6.7 P5 loss: a real `testTimeout (FAILED)` was correctly classified Error-class but
// EVICTED from the metalog reservoir in a high-cardinality window — non-failure salience
// (novelty / structural-surprise) out-competed the low-frequency failure for the bounded M
// slots. The fix is at RETENTION, not the eidos significance cut (which is downstream of the
// loss): a bounded floor of M slots is reserved for error-class templates (dominant_level ∈
// {Error,Fatal} or Terminator) and admitted in Phase 1 — AHEAD of the general pool and
// EXEMPT from the per-kind cap — so non-failure salience can no longer evict a real failure.
//
// THE RED (paired with its negative control — the file's RareError{Admitted,NotRetained}
// idiom): the SAME high-cardinality window, with reservoir_error_reserve OFF vs ON. The
// window holds a steady top_k bed, K=3 distinct STRONG-off-path benign Info branches whose
// structural-surprise salience (StrongOffPath=90 × rarity) OUT-SCORES the rare Error failure
// (Error=80 × rarity), and one rare Error failure below top_k by frequency. With the reserve
// OFF the surprise branches fill M and the failure is tail dust; with the reserve ON the
// failure is guaranteed a slot DESPITE its lower salience — a branch yields its slot to it.
// [[sift-failure-lexicon-must-be-outcome-aware]] [[sift-forcing-corpus-fatigue-vs-catch]]
namespace
{
    constexpr std::array<std::string_view, 3> kSurpriseBranches{
        "took alternate cache path", "took fallback dns route", "took degraded retry queue"};

    // Build a high-cardinality window: dominant path A→B→C ×200 (fills top_k), then K=3
    // recurring off-path branches A→B→Xi→C ×3 each (p = 3/209 < 2% → StrongOffPath surprise 90),
    // then one rare Error failure (below top_k, salience 80×rarity < the branches' 90×rarity).
    [[nodiscard]] meta::MetaLogDocument build_high_card_window(std::size_t error_reserve)
    {
        meta::MetaLogEngine engine{
            meta::MetaLogConfig{.top_k_size = 3,
                                .reservoir_size = 3,
                                .reservoir_per_kind_cap = 0, // isolate the reserve
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
    // Reserve ON: the rare Error failure is GUARANTEED a slot; one surprise branch yields.
    const auto with_reserve{build_high_card_window(/*error_reserve=*/1)};
    ASSERT_TRUE(reservoir_has(with_reserve, "connection refused to db"))
        << "the error-class reserve must retain the rare real failure in a high-card window";
    EXPECT_FALSE(top_k_has(with_reserve, "connection refused to db"))
        << "the failure is below top_k by frequency — retention is the reserve's doing, not top_k";
    EXPECT_EQ(branches_retained(with_reserve), 2U)
        << "exactly one higher-salience non-failure branch yielded its slot to the reserved "
           "failure";

    // The reserve overrode salience ORDER: the retained failure scores LESS than the branches
    // that kept their slots — proving it was admitted by the reserve, not by out-scoring them.
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

    // The negative control (reserve OFF): the identical window evicts the failure — the
    // surprise storm fills M and the real failure is tail dust. This is the P5 loss, made
    // a standing assertion; the reserve flips it 0→1.
    const auto no_reserve{build_high_card_window(/*error_reserve=*/0)};
    EXPECT_FALSE(reservoir_has(no_reserve, "connection refused to db"))
        << "without the reserve, non-failure salience evicts the real failure (the §6.7 P5 loss)";
    EXPECT_EQ(branches_retained(no_reserve), 3U)
        << "without the reserve, all three higher-salience non-failure branches keep the slots";
}

// The reserve is EXEMPT from the per-kind diversity cap (load-bearing — §5.2). The cap
// governs only the GENERAL pool: it bounds how many exemplars of one (role×level) kind the
// pool admits, which would otherwise limit error-class templates too. The reserve admits
// error-class failures ahead of and outside that cap, so MULTIPLE distinct failures survive
// even when the cap would keep only one in the general pool.
TEST(ReservoirTest, ErrorClassReserveIsExemptFromPerKindCap)
{
    const auto build{
        [](std::size_t error_reserve)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{
                .top_k_size = 3,
                .reservoir_size = 4,
                .reservoir_per_kind_cap = 1, // general pool: ≤1 per (role×level) kind
                .reservoir_error_reserve = error_reserve,
                .emit_stability = false}};
            engine.open_window(std::chrono::system_clock::time_point{});
            for (int rep = 0; rep < 100; ++rep)
            {
                engine.ingest_event(make_event("alpha steady event"));
                engine.ingest_event(make_event("beta steady event"));
                engine.ingest_event(make_event("gamma steady event"));
            }
            // Four DISTINCT rare Error failures — all the SAME kind (None × Error),
            // so the per-kind cap=1 admits only ONE via the general pool.
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

// ── D-PROV-1 (§3.1) — the echoed-source salience gate, at the ENGINE altitude ───
// The salience FUNCTION is locked by stats:SalienceScore.EchoedSourceSkipsFailureCueTier.
// This is its engine-level twin: it proves the bucket-level `all_echoed_source` AND-reduction
// (engine.cpp:244 — a template is "all echoed" only while EVERY event forming it is echoed
// source; one runtime occurrence makes it false) AND that the engine threads it into the
// salience computation, so an all-echoed `…failed…` template (level already demoted to
// Unknown by canon A1) is NOT admitted to the reservoir, while the SAME text seen once as a
// real runtime event IS. The function lock cannot catch a regression in that wiring/reduction.
TEST(ReservoirTest, AllEchoedFailureTemplateNotAdmittedButRuntimeOccurrenceRescues)
{
    const auto echoed_event{[](std::string_view tmpl)
                            {
                                // canon A1 demotes an echoed line's level to Unknown and sets
                                // the flag; mirror that exact shape here.
                                auto ev{make_event(tmpl, insight::LogLevel::Unknown)};
                                ev.echoed_source = true;
                                return ev;
                            }};

    // The echoed run is ingested FIRST and self-loops (first-seen ≈ 0 → novelty 0; self-loop →
    // structural-surprise 0), so the failure-cue tier is the ONLY axis that could make it
    // salient — isolating the echoed gate. (A LATE-emerging template would be lifted by novelty
    // regardless of the gate, which would not test the gate at all.)

    // (a) An ALL-echoed failure template — every occurrence is echoed CI script source.
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

    // (b) The AND-reduction minimal pair: the SAME template seen ONCE as a real runtime event
    // (echoed_source=false) makes the bucket NOT all-echoed → the failure-cue tier stands → it
    // is admitted. One genuine runtime occurrence rescues the failure's salience.
    {
        meta::MetaLogEngine engine{
            meta::MetaLogConfig{.top_k_size = 3, .reservoir_size = 8, .emit_stability = false}};
        engine.open_window(std::chrono::system_clock::time_point{});
        engine.ingest_event(echoed_event("Download failed after 3 attempts")); // echoed
        engine.ingest_event(echoed_event("Download failed after 3 attempts")); // echoed
        engine.ingest_event(make_event("Download failed after 3 attempts"));   // runtime!
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

// SPEC §3.7.2 normative MUST: salience admission is salience-ranked with a
// deterministic **tie-break by template_id**, so a given input under a given
// retention_profile yields a bit-identical reservoir. Two templates with equal
// salience (same level, same count, no other axis differentiating) admitted into
// a 1-slot reservoir: the smaller template_id wins.
TEST(ReservoirTest, TieBreakByTemplateIdAtEqualSalience)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 0,     // every template is a reservoir candidate
        .reservoir_size = 1, // exactly one slot — the tie must be broken
    }};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    // Two Error-level templates, count 1 each, no failure-lexicon words and no
    // structural surprise/novelty: salience comes purely from level → identical.
    engine.ingest_event(make_event("alpha", insight::LogLevel::Error));
    engine.ingest_event(make_event("beta", insight::LogLevel::Error));
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};

    ASSERT_EQ(doc.stats.reservoir.size(), 1U);
    const auto tid_alpha{insight::template_id_of("alpha")};
    const auto tid_beta{insight::template_id_of("beta")};
    ASSERT_NE(tid_alpha, tid_beta);
    EXPECT_EQ(doc.stats.reservoir[0].template_id, std::min(tid_alpha, tid_beta))
        << "§3.7.2: at equal salience, the smaller template_id wins (got "
        << doc.stats.reservoir[0].template_id
        << "; min(tid_alpha,tid_beta)=" << std::min(tid_alpha, tid_beta) << ")";
}

// ── §15 re-derivation coordinate ──────────────────────────────────────────────

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
    // §15.2 RAW coordinate: source_ref + bounds present, children absent.
    ASSERT_TRUE(doc.coordinate->source_ref.has_value());
    EXPECT_EQ(doc.coordinate->source_ref->resolver_kind, "logcraft");
    EXPECT_EQ(doc.coordinate->source_ref->handle, "scenario#seed=7");
    ASSERT_TRUE(doc.coordinate->bounds.has_value());
    // Bounds are the window's EVENT-TIME integer ticks, exactly (§15.3).
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
    // 20 benign events fill top_k; the rare error first appears at ordinal 20.
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
                << "§15.4 sub-coordinate must be populated when a coordinate is configured";
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
    // §15.2 COMPOSED coordinate: source_ref + bounds ABSENT (no sentinel — §15.2
    // explicitly forbids them on composed); children present, addressing raw kids.
    EXPECT_FALSE(composed.coordinate->source_ref.has_value())
        << "a composed coordinate MUST NOT carry source_ref (§15.2)";
    EXPECT_FALSE(composed.coordinate->bounds.has_value())
        << "a composed coordinate MUST NOT carry bounds (§15.2) — children are authoritative";
    ASSERT_TRUE(composed.coordinate->children.has_value());
    ASSERT_EQ(composed.coordinate->children->size(), 2U)
        << "the set of the two raw children (§15.5)";
    // Each child is a RAW coordinate addressing the input — source_ref + bounds set.
    ASSERT_TRUE((*composed.coordinate->children)[0].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[0].source_ref->handle, "scenario#seed=1");
    ASSERT_TRUE((*composed.coordinate->children)[1].source_ref.has_value());
    EXPECT_EQ((*composed.coordinate->children)[1].source_ref->handle, "scenario#seed=2");
}
// Wire-level XOR (§15.2 encoding note): a composed coordinate's JSON has `children`
// and MUST NOT carry `source_ref` or `bounds` (no sentinel emission).
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
    // This test asserts the coordinate XOR encoding, not template strings — an empty registry is
    // sufficient (composed docs are id-only; their display strings resolve from the engine
    // registry).
    const std::string json{meta::to_json(composed, meta::TemplateRegistry{})};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << "serialised composed doc did not parse: " << json;
    ASSERT_TRUE((*parsed).contains("coordinate")) << json;
    auto& coord{(*parsed)["coordinate"]};
    EXPECT_TRUE(coord.contains("children")) << "composed coordinate must carry children\n" << json;
    EXPECT_FALSE(coord.contains("source_ref"))
        << "§15.2: composed coordinate MUST NOT carry source_ref\n"
        << json;
    EXPECT_FALSE(coord.contains("bounds"))
        << "§15.2: composed coordinate MUST NOT carry bounds (no sentinel)\n"
        << json;
}

} // namespace

// NOLINTEND
