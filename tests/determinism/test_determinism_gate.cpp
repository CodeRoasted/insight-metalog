// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// BEHAVIORAL coverage for the metalog document (the ADR-31.D8 reservoir regime, the always-on
// cube/WHERE/acquisition fields, the ordinal carrier). The cross-machine BYTE-IDENTITY
// determinism proof is a CUT/GATE-TIME assertion, NOT a unit test: the 5-leg cross-toolchain/
// ISA/OS `Determinism Golden Proof` workflow (.github/workflows/golden.yaml) rebuilds the
// canon+metalog tower from source and asserts all legs are byte-identical over the committed
// corpus + the shared ADR-31.D8 reservoir scenario (scripts/determinism_bitidentity.sh). There is
// NO committed golden hash here (or anywhere) — determinism is proven by cross-leg AGREEMENT,
// emitted as a per-release artifact only. These tests keep the SCENARIOS non-hollow (they exercise
// the regimes the gate replays) and pin the derived field VALUES.

#include <gtest/gtest.h>

import insight.metalog.test;

// The shared ADR-31.D8 near-full reservoir scenario, shared with scripts/determinism_fixture.cpp so
// this behavioral coverage and the cross-leg gate exercise the identical M=128 admit/evict
// boundary.
#include "reservoir_nearfull_scenario.hpp"
// The SECOND ADR-31.D8 reservoir arm — the tuple the streaming surface ships
// (`salience-1/k128-m64-c0-e16`), where the error-class RESERVE is live and the batch arm has no
// opinion. Same sharing contract as above.
#include "reservoir_streaming_scenario.hpp"
// The shared O4b service-topology over-cap scenario, shared with the fixture so this guard and the
// cross-leg gate exercise the identical over-cap top-K select (the canonical-key tie-break).
#include "service_edges_overcap_scenario.hpp"
// The SPEC §4 accounting-bound scenario — the ONE window in the whole determinism digest whose
// emitted document CARRIES `behavior.dropped_ngram_observations`. Same sharing contract as above.
#include "ngram_cap_scenario.hpp"

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// The near-full reservoir MUST fill to the full production M and its admit/evict boundary MUST be
// structural-surprise-driven — the guard that keeps the ADR-31.D8 regime exercised. The cross-leg
// gate replays the same scenario; if it silently stopped filling, that proof would go hollow.
TEST(MetaLogDocument, ReservoirNearFullExercisesTheF5M8Regime)
{
    meta::MetaLogConfig cfg;
    meta::nearfull::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    engine.open_window(t0);
    meta::nearfull::emit_window(engine);
    const auto doc{engine.close_window(t1)};

    std::size_t surprise_driven{0};
    for (const auto& entry : doc.stats.reservoir)
        if (entry.structural_surprise > 0 && entry.dominant_level != insight::LogLevel::Error &&
            entry.dominant_level != insight::LogLevel::Fatal)
            ++surprise_driven;
    ASSERT_EQ(doc.stats.reservoir.size(), cfg.reservoir_size)
        << "near-full reservoir fixture must fill the item-reservoir to the full production M="
        << cfg.reservoir_size << " (got " << doc.stats.reservoir.size()
        << ").\n  unique_templates=" << doc.stats.unique_templates
        << " top_k=" << doc.stats.top_k.size()
        << " surprise_driven_reservoir_entries=" << surprise_driven;
    EXPECT_GT(surprise_driven, 0U)
        << "the reservoir boundary must be structural_surprise-driven so the ADR-31.D8 hazard "
           "(a non-deterministic surprise score) flips bag membership; none were.";
}

// The SECOND ADR-31.D8 arm, at the tuple the STREAMING surface ships
// (`salience-1/k128-m64-c0-e16`). The arm above is anchored at `top_k 64 / M 128 / cap 0 /
// reserve 0` — the Sift batch diff — so without this one the flagship determinism proof never runs
// at the configuration we deploy, and the error-class RESERVE (live only here) is never driven at
// all. This guard keeps the scenario NON-HOLLOW; the bit-identity assertion itself is the cross-leg
// gate's (determinism_bitidentity.sh + golden.yaml), which replays the identical window.
//
// Every expectation below is a property the cross-leg gate would go blind without, and each one can
// FAIL — measured, by inverting engine.cpp's most-likely-edge tie-break to prefer the FEWER-
// observation edge: `error_class` 16 -> 24 and `ambiguous` 8 -> 0, two reds, while
// `reservoir.size()` stayed at 64. A size-only guard would have called that regression green.
TEST(MetaLogDocument, ReservoirStreamingExercisesTheShippedTupleReserveAndEdgeTie)
{
    namespace streaming = meta::streaming_nearfull;

    meta::MetaLogConfig cfg;
    streaming::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    engine.open_window(t0);
    streaming::emit_window(engine);
    const auto doc{engine.close_window(t1)};

    // Group membership is read off the interned template text, not off a derived band, so a failure
    // names the population that moved rather than a number.
    std::size_t error_class{0};
    std::size_t ambiguous{0};
    std::size_t ambiguous_at_full_surprise{0};
    std::size_t solid{0};
    for (const auto& entry : doc.stats.reservoir)
    {
        const std::string_view tmpl{engine.registry().lookup(entry.template_id)};
        if (entry.structural_role == insight::StructuralRole::Terminator ||
            entry.dominant_level == insight::LogLevel::Error ||
            entry.dominant_level == insight::LogLevel::Fatal)
            ++error_class;
        if (tmpl.starts_with("ambiguous boundary spoke"))
        {
            ++ambiguous;
            if (entry.structural_surprise == 90U)
                ++ambiguous_at_full_surprise;
        }
        if (tmpl.starts_with("dispatched task"))
            ++solid;
    }

    const auto census = [&]
    {
        return "  reservoir=" + std::to_string(doc.stats.reservoir.size()) + "/" +
               std::to_string(cfg.reservoir_size) + " error_class=" + std::to_string(error_class) +
               " solid=" + std::to_string(solid) + " ambiguous=" + std::to_string(ambiguous) +
               " (at surprise 90: " + std::to_string(ambiguous_at_full_surprise) +
               ") unique_templates=" + std::to_string(doc.stats.unique_templates) +
               " top_k=" + std::to_string(doc.stats.top_k.size());
    };

    ASSERT_EQ(doc.stats.reservoir.size(), cfg.reservoir_size)
        << "the streaming fixture must fill the item-reservoir to the full shipped M="
        << cfg.reservoir_size
        << "; a short reservoir means the admit/evict BOUNDARY is never "
           "reached and the arm proves nothing.\n"
        << census();

    // ADR-20.D7, measured. The general pool is over-subscribed at the 8100 band by 64 candidates
    // for 48 slots, so with `reservoir_error_reserve = 0` NOT ONE error-class template survives.
    // Exactly kErrorReserve says both halves at once: the reserve admits, and it BOUNDS (24
    // error-class candidates compete for 16 slots, and the 8 that lose do not sneak in through the
    // general pool).
    EXPECT_EQ(error_class, streaming::kErrorReserve)
        << "the error-class reserve is the ONLY reason a failure is retained in this window, so "
           "this count is the reserve itself. Anything else means the reserve stopped binding "
           "(more) or stopped admitting (fewer), and the shipped `e16` axis is no longer measured "
           "by anything.\n"
        << census();

    // The equal-ratio edge tie-break, at the boundary. 40 solid + 24 ambiguous share the 8100 band
    // for 48 free slots, so at least (48 - 40) ambiguous MUST be admitted and at least 16 of the
    // band MUST be rejected. Lose the tie-break and every ambiguous spoke falls to 3600 and the
    // error class's reserve overflow takes those seats — which is exactly what the cross-leg gate
    // would then see as moved bytes.
    ASSERT_GE(ambiguous,
              streaming::kGeneralSlots - static_cast<std::size_t>(streaming::kSolidSpokes))
        << "the ambiguous equal-ratio spokes lost their seats at the reservoir boundary: the "
           "most-likely-edge TIE is no longer resolved to the higher-observation edge, or the "
           "window stopped over-subscribing the 8100 band.\n"
        << census();
    EXPECT_LT(ambiguous, static_cast<std::size_t>(streaming::kAmbiguousSpokes))
        << "every ambiguous spoke was admitted, so the boundary no longer CUTS the tie group and a "
           "surprise-score divergence could no longer change the bag.\n"
        << census();
    EXPECT_EQ(ambiguous_at_full_surprise, ambiguous)
        << "an admitted ambiguous spoke carries a structural_surprise other than 90 — the tie "
           "resolved to the single-observation edge, which is the ADR-31.D8 defect itself.\n"
        << census();

    // The general pool is filled by the 8100 band ALONE — which is what makes the two statements
    // above compose into one fact: every retained failure is there because of the reserve, and
    // every general slot is decided at the equal-ratio boundary.
    EXPECT_EQ(solid + ambiguous, streaming::kGeneralSlots)
        << "a template from below the 8100 band reached the general pool, so the pool is no longer "
           "saturated by the solid/ambiguous tie group and the boundary has drifted off the "
           "equal-ratio spokes this arm exists to drive.\n"
        << census();
}

// O4b service-topology (SRC-D-OTEL-21): the over-cap top-K select MUST stay non-hollow — the
// emitted block must be OVER the cap (dropped_edges > 0) AND the cut must fall on a weight tie, so
// the surviving last edge is decided by the canonical-key tie-break alone (the branch the cross-leg
// gate proves bit-identical). If the scenario silently stopped over-subscribing, or the tie
// collapsed, that proof would go hollow. Also pins the derived edge VALUES (order, weights, dropped
// count).
TEST(MetaLogDocument, ServiceEdgesOverCapExercisesTheTieBreak)
{
    meta::MetaLogConfig cfg;
    meta::service_edges_overcap::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    engine.open_window(t0);
    meta::service_edges_overcap::emit_window(engine);
    const auto doc{engine.close_window(t1)};

    ASSERT_TRUE(doc.service_edges.has_value())
        << "a span window with cross-service edges must emit the service_edges block";
    const auto& block{*doc.service_edges};
    // Non-hollowness: the block is truncated (over-cap select taken) and the cut fell on a tie.
    ASSERT_EQ(block.edges.size(), cfg.max_service_edges)
        << "the block must be capped at max_service_edges=" << cfg.max_service_edges << " (got "
        << block.edges.size() << ") — else the over-cap select path is not exercised";
    EXPECT_EQ(block.dropped_edges, 2U)
        << "5 distinct edges built, 3 kept → 2 dropped; a 0 here means the scenario went hollow";
    // The surviving wire, in canonical (caller, callee) order. The 3rd edge — {gateway,auth} — is
    // the load-bearing one: it beat {gateway,billing} and {worker,queue} (all weight 2) on the
    // canonical-key tie-break alone. A stdlib-order-dependent select would surface a DIFFERENT 3rd
    // edge here, and this expectation would fail on that leg.
    const auto row = [&](std::size_t i)
    {
        return std::tuple{block.edges.at(i).caller, block.edges.at(i).callee,
                          block.edges.at(i).weight};
    };
    EXPECT_EQ(row(0), (std::tuple<std::string, std::string, std::uint64_t>{"api", "cache", 4U}));
    EXPECT_EQ(row(1), (std::tuple<std::string, std::string, std::uint64_t>{"api", "db", 5U}));
    EXPECT_EQ(row(2), (std::tuple<std::string, std::string, std::uint64_t>{"gateway", "auth", 2U}))
        << "the tie-break must keep the canonical-smallest weight-2 key {gateway,auth}; a "
           "different "
           "3rd edge means the top-K select is stdlib-order-dependent (non-deterministic).";
}

// SPEC §4 accounting bound: the `--ngram-cap` scenario MUST stay non-hollow — the window's bigram
// stream must genuinely OVERRUN `max_ngram_keys`, so the emitted document CARRIES
// `behavior.dropped_ngram_observations` rather than omitting it. That carrying is the entire
// reason the section exists: every other section of the digest (the committed corpus and the four
// sibling scenarios) stays under the bound, and §4 reads an absent key in a 0.7.0+ document as an
// affirmative "nothing was dropped" — so if this scenario stopped binding, the digest would go
// back to containing zero documents with the key, the cross-leg compare and the §8 validator would
// both stay green, and the coverage would be gone with nothing red.
//
// THE COUNT IS DERIVED, NOT COPIED. `expected_dropped_observations(cfg)` recomputes
// `kDistinctTemplates - 1 - max_ngram_keys` from the config in hand, so a moved producer default
// reds here with its arithmetic instead of reding on a stale literal. The one literal kept is the
// SHIPPED value (1903 at ngram 2 / cap 4096, hand-checked), asserted beside it — a derivation that
// agrees with itself proves nothing, and this pair is what separates "the formula still holds"
// from "the cut Sift embeds still produces this number".
TEST(MetaLogDocument, NgramCapBindsSoTheDigestCarriesTheDroppedObservationCount)
{
    meta::MetaLogConfig cfg;
    meta::ngram_cap::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    engine.open_window(t0);
    meta::ngram_cap::emit_window(engine);
    const auto doc{engine.close_window(t1)};

    // The scenario precondition, not the property: if the templates collapsed into fewer shapes
    // the bigram arithmetic below is arbitrary rather than wrong.
    ASSERT_EQ(doc.stats.unique_templates, meta::ngram_cap::kDistinctTemplates)
        << "each of the " << meta::ngram_cap::kDistinctTemplates
        << " one-shot strings must form its OWN template for the bigram count to hold; got "
        << doc.stats.unique_templates;

    // The block must EXIST before its member can be read — a block-absent document would satisfy a
    // naive "the field is not what I expected" reading while testing nothing.
    ASSERT_TRUE(doc.behavior.has_value())
        << "top_ngrams_size is non-zero and the stream formed n-grams, so the §4 block is owed";
    ASSERT_FALSE(doc.behavior->top_ngrams.empty())
        << "a populated behavior block, or the count below is about an empty section";

    ASSERT_TRUE(doc.behavior->dropped_ngram_observations.has_value())
        << "THE SECTION WENT HOLLOW: the bound refused nothing, so this document omits the key and "
           "the digest is back to carrying zero documents that have it. "
        << meta::ngram_cap::kDistinctTemplates << " templates form "
        << (meta::ngram_cap::kDistinctTemplates - 1) << " bigrams against a cap of "
        << cfg.max_ngram_keys;
    EXPECT_EQ(*doc.behavior->dropped_ngram_observations,
              meta::ngram_cap::expected_dropped_observations(cfg))
        << "derived: " << meta::ngram_cap::kDistinctTemplates << " - 1 - " << cfg.max_ngram_keys;
    EXPECT_EQ(*doc.behavior->dropped_ngram_observations, std::uint64_t{1903})
        << "the SHIPPED number, at the cut sift embeds (ngram 2 / max_ngram_keys 4096). If this "
           "reds while the derived assertion above stays green, a producer default moved and the "
           "scenario is no longer anchored on the configuration that ships.";

    // The admitted table filled exactly to its bound, which is what makes the refusal count a
    // statement about the CAP rather than about a stream that ran short.
    EXPECT_EQ(doc.behavior->top_ngrams.size(), cfg.top_ngrams_size)
        << "every admitted bigram is at count 1, so the top-N select runs entirely on its "
           "`sequence` tie-break over "
        << cfg.max_ngram_keys
        << " candidates — a tie-break that stopped being a total order surfaces as a cross-leg "
           "byte difference and nowhere else in this corpus";
}

// The always-on document (1.7.2): cube + WHERE + acquisition are unconditional. Pins the
// per-template dominant_component tie-break (ties → component string ascending, a pure content
// function) and the acquisition dimension-metadata VALUES (Piece 2). The bytes' cross-machine
// identity is the cross-leg gate's job; here we pin the values.
TEST(MetaLogDocument, AlwaysOnCubeWhereAndAcquisitionFields)
{
    // component is a string_view INTO the event; string literals have static storage, so the views
    // stay valid for the whole test. Empty component → a free-text line carrying no WHERE.
    const auto ev = [](std::string_view tmpl, insight::LogLevel level, std::string_view component)
    {
        tok::CanonicalEvent e;
        e.template_str = tmpl;
        e.level = level;
        e.component = component;
        return e;
    };

    meta::MetaLogConfig cfg; // cube + WHERE + acquisition are all always-on now
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    // Window 1: PARTIAL coverage (a free-text template carries no component) over four distinct
    // components — one a per-template TIE (ping: zebra×2, alpha×2) → ascending picks "alpha".
    engine.open_window(t0);
    for (int i = 0; i < 6; ++i)
        engine.ingest_event(ev("login ok", insight::LogLevel::Info, "auth"));
    for (int i = 0; i < 4; ++i)
        engine.ingest_event(ev("query slow", insight::LogLevel::Warn, "db"));
    for (int i = 0; i < 2; ++i)
        engine.ingest_event(ev("ping", insight::LogLevel::Info, "zebra"));
    for (int i = 0; i < 2; ++i)
        engine.ingest_event(ev("ping", insight::LogLevel::Info, "alpha"));
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(ev("starting up", insight::LogLevel::Info, ""));
    const auto doc1{engine.close_window(t1)};

    // Window 2: a db ERROR burst over steady auth traffic; FULL coverage.
    engine.open_window(t1);
    for (int i = 0; i < 6; ++i)
        engine.ingest_event(ev("login ok", insight::LogLevel::Info, "auth"));
    for (int i = 0; i < 5; ++i)
        engine.ingest_event(ev("pool timeout", insight::LogLevel::Error, "db"));
    const auto doc2{engine.close_window(t2)};

    ASSERT_TRUE(doc1.has_cube) << "the cube is always built (always-on)";
    ASSERT_TRUE(doc2.has_cube);
    ASSERT_TRUE(doc1.acquisition.has_value())
        << "the per-window acquisition block is always emitted";
    ASSERT_TRUE(doc2.acquisition.has_value());
    EXPECT_EQ(doc1.acquisition->records_with_component, 14U)
        << "located events = 6 auth + 4 db + 2 zebra + 2 alpha; the 3 free-text lines carry none";
    EXPECT_EQ(doc1.acquisition->distinct_components, 4U) << "auth, db, alpha, zebra";
    EXPECT_EQ(doc2.acquisition->records_with_component, 11U) << "6 auth + 5 db (all located)";
    EXPECT_EQ(doc2.acquisition->distinct_components, 2U) << "auth, db";
    // Dimension-metadata (Piece 2): the collapse guardrail's raw trigger inputs.
    EXPECT_EQ(doc1.acquisition->where_cardinality_per_depth,
              (std::vector<std::uint64_t>{doc1.acquisition->distinct_components}))
        << "depth-1 WHERE tree → one per-depth entry == distinct_components";
    EXPECT_EQ(doc1.acquisition->closed_cells, doc1.cube.cell_count)
        << "P_closed == the closed cube's cell count";
    // Per-dimension cardinality (the mandatory cardinality signal): distinct count per cube axis.
    EXPECT_EQ(doc1.acquisition->level_cardinality, 2U) << "distinct levels observed: INFO, WARN";
    EXPECT_EQ(doc1.acquisition->role_cardinality, 1U) << "distinct roles observed: None only";
    std::set<std::string> labels;
    for (const auto& entry : doc1.stats.top_k)
        if (entry.dominant_component)
            labels.insert(*entry.dominant_component);
    EXPECT_EQ(labels, (std::set<std::string>{"alpha", "auth", "db"}))
        << "ping ties zebra/alpha → ascending picks 'alpha' (never zebra); the free-text template "
           "carries no label (disengaged, never \"\")";
}

// The W1 ordinal binned-carrier must be populated (the carrier the eidos W1 distance rides).
TEST(MetaLogDocument, OrdinalCarrierPopulated)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 2; // the batch / full-fidelity gate the ordinal carrier rides
    meta::MetaLogEngine engine{cfg};
    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};

    // A deterministic latency spread across several octaves on one template (ms → ns by ×1e6).
    constexpr std::array<std::int64_t, 6> kLatenciesMs{12, 45, 130, 480, 1100, 6000};
    constexpr std::int64_t kNanosPerMilli{1'000'000};
    engine.open_window(t0);
    for (int i = 0; i < 120; ++i)
    {
        const std::array<insight::OrdinalObservation, 1> obs{
            {{.field_name = "latency_ms",
              .schedule = insight::OrdinalSchedule::DurationLog2Ns,
              .value = kLatenciesMs.at(static_cast<std::size_t>(i) % kLatenciesMs.size()) *
                       kNanosPerMilli}}};
        tok::CanonicalEvent ev;
        ev.template_str = "db query completed";
        ev.ordinals = obs; // span valid through ingest
        engine.ingest_event(ev);
    }
    const auto doc{engine.close_window(t1)};

    ASSERT_FALSE(doc.stats.top_k.empty());
    const auto& entry{doc.stats.top_k.front()};
    ASSERT_EQ(entry.ordinal_histograms.size(), 1U)
        << "the ordinal carrier must be populated for the W1 distance to mean anything";
    EXPECT_EQ(entry.ordinal_histograms.front().field_name, "latency_ms");
    EXPECT_EQ(entry.ordinal_histograms.front().schedule_id, "dur-log2-ns-v1");
    EXPECT_EQ(entry.ordinal_histograms.front().total, 120U);
}

} // namespace
// NOLINTEND
