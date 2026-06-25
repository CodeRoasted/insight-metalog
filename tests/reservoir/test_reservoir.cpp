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
    auto doc{engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60})};
    if (out_registry != nullptr)
        *out_registry = engine.registry(); // D-TIR-5: caller serialises via the engine registry
    return doc;
}

// D-TIR-5 field-drop: entries carry only the content-derived TemplateId now (template_str moved to the
// registry). A membership check computes the expected id from the string (the stateless masker is a
// pure fn — same string → same id the engine assigned) and compares ids; no registry needed.
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
    // sufficient (composed docs are id-only; their display strings resolve from the engine registry).
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
