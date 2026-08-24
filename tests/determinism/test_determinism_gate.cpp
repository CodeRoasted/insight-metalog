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
// The §13.6 latency-drift window PAIR — the only scenario here whose replayed artifact is a
// MetaLogDiff rather than a MetaLogDocument. Same sharing contract as above.
#include "latency_shift_scenario.hpp"
// The §16.10 compare-at-min PAIR — two cubes at different collapse depths. Same contract.
#include "collapse_depths_scenario.hpp"

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

// ── --latency-shift: the digest's SECOND artifact species ──────────────────────────────
//
// The other scenarios here guard a window; this one guards a PAIR, because the artifact the
// cross-leg gate replays for it is a MetaLogDiff. Two things can go hollow independently and
// each would leave the digest section looking exactly as healthy as a working one:
//
//   1. The DRIFT stops being admissible. `component_latency_shifts` skips a component whose
//      paired sample is under kShiftSampleFloor (32) — correctly, and SILENTLY: the axis is
//      projected away and the section emits a plain 3-D border. So the "did the mechanism
//      run" assertion comes first, on the ordinal carrier both sides must carry.
//   2. The SERIALIZER stops emitting the block. A domain object that holds `cube_diff` is not
//      a digest that carries it, and the two are checked in different places. This producer
//      already has one member that is computed every diff and reaches no wire at all
//      (`field_histogram_deltas`, SPEC §3.5.2's declared wire-emission status), so "computed
//      but dropped at the DTO" is a live failure mode here, not a hypothetical one — which is
//      why the last assertion reads the JSON and not the struct.
//
// What this deliberately does NOT assert: `axes[].kind`. The scenario's job is to PRODUCE the
// differential axis; how the standard spells its kind is metalog-spec's to decide, and
// pinning today's spelling here would make this guard red at the moment that decision lands.
// The conformance gate (scripts/spec_conformance_gate.sh) is what judges the spelling, against
// the published schema rather than against our own belief about it.
TEST(MetaLogDocument, LatencyShiftPairEmitsTheDifferentialAxisOnTheWire)
{
    meta::MetaLogConfig cfg;
    meta::latency_shift::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};
    engine.open_window(t0);
    meta::latency_shift::emit_window(engine, meta::latency_shift::kPreviousLatencyMs);
    const auto previous{engine.close_window(t1)};
    engine.open_window(t1);
    meta::latency_shift::emit_window(engine, meta::latency_shift::kCurrentLatencyMs);
    const auto current{engine.close_window(t2)};

    // (1) Did the mechanism run? Both sides must carry a duration ladder on `payments` with at
    // least kShiftSampleFloor paired observations, or every assertion below is about a window
    // where the axis was correctly, invisibly, projected away.
    const auto duration_total{[](const meta::MetaLogDocument& doc)
                              {
                                  std::uint64_t total{0};
                                  for (const auto& entry : doc.stats.top_k)
                                      if (entry.dominant_component == "payments")
                                          for (const auto& hist : entry.ordinal_histograms)
                                              if (hist.schedule_id == "dur-log2-ns-v1")
                                                  total += hist.total;
                                  return total;
                              }};
    const std::uint64_t previous_total{duration_total(previous)};
    const std::uint64_t current_total{duration_total(current)};
    constexpr std::uint64_t kShiftSampleFloor{32}; // diff.cpp ComponentOrdinal, studies/003
    ASSERT_GE(std::min(previous_total, current_total), kShiftSampleFloor)
        << "the shift is INADMISSIBLE below the floor and the axis is silently projected away, "
           "so this scenario would emit a plain 3-D border while still looking healthy.\n"
           "  payments duration observations: previous="
        << previous_total << " current=" << current_total << " floor=" << kShiftSampleFloor;

    const meta::MetaLogDiff delta{meta::diff(previous, current)};
    ASSERT_TRUE(delta.has_cube_diff)
        << "both windows carry a cube, so the pair must produce a cube_diff — without one the "
           "digest's --latency-shift section carries no differential axis at all";

    // (2) The differential axis is declared, by NAME. It is emergent-at-diff: no stored cube
    // ever carries it, so its presence here is the whole reason this section exists.
    std::vector<std::string> axis_names;
    for (const auto& axis : delta.cube_diff.axes)
        axis_names.push_back(axis.name);
    EXPECT_NE(std::ranges::find(axis_names, "latency_shift"), axis_names.end())
        << "a component shifted, so the diff-only latency_shift axis must be declared.\n"
           "  axes declared: "
        << [&axis_names]
    {
        std::string out;
        for (const auto& name : axis_names)
            out += (out.empty() ? "" : ", ") + name;
        return out;
    }();

    // (3) The band, pinned by VALUE. 100 ms → 100 s is 10 octaves on the frozen log2-ns ladder,
    // deep inside HIGH; a ladder or threshold retune that moved this scenario onto a different
    // band would otherwise change the digest's bytes with nothing here to say why.
    const meta::CubeBorderCell* shifted{nullptr};
    if (delta.cube_diff.has_emerging)
        for (const auto* region :
             {&delta.cube_diff.emerging.lower, &delta.cube_diff.emerging.upper})
            for (const auto& cell : *region)
                if (cell.coord.latency_shift && cell.coord.where && !cell.coord.where->empty() &&
                    cell.coord.where->back() == "payments")
                    shifted = &cell;
    ASSERT_NE(shifted, nullptr)
        << "expected an EMERGING (where=payments, latency_shift=…) border cell — the shift only "
           "ever pins on the current side, so it participates in emergence alone";
    EXPECT_EQ(*shifted->coord.latency_shift, "up_high")
        << "a 10-octave UP move must land in the HIGH band; got " << *shifted->coord.latency_shift;

    // (4) The WIRE, not the struct. This is the assertion that separates "the engine computed a
    // cube_diff" from "the digest carries one".
    const std::string wire{meta::to_json(delta)};
    EXPECT_NE(wire.find("\"cube_diff\""), std::string::npos)
        << "to_json(MetaLogDiff) dropped the cube_diff block — the domain object has it and the "
           "serialized artifact the gate judges does not.\n  wire: "
        << wire;
    EXPECT_NE(wire.find("\"latency_shift\""), std::string::npos)
        << "the differential axis reached no wire, so the digest section is decoration.\n  wire: "
        << wire;
}

// ── --collapse-depths: the pair whose diff axes equal NEITHER input's ──────────────────
//
// §16.10 mandates that a diff of two cubes be read at the COARSER of the two on each axis.
// §13.6's example text carries an unbolded comment saying `cube_diff.axes` equals both inputs'
// `cube.axes` — which is not normative in any of §13.6's bullets, and which this pair refutes by
// construction. So this guard asserts CONTAINMENT and the collapse arithmetic, and deliberately
// asserts no equality: writing the equality arm would pin a claim the standard does not make and
// would red the day §16.10 is exercised, which is here.
//
// It goes hollow the moment the previous window stops firing the banding guardrail — the two
// cubes would then share a collapse state, `min_common_collapse` would be a no-op, and the pair
// would silently become an ordinary same-depth diff that still emits a healthy-looking cube_diff.
// That is why the collapse states are read off the two documents FIRST.
TEST(MetaLogDocument, CollapseDepthsPairIsReadAtTheMinimalCommonCollapse)
{
    meta::MetaLogConfig cfg;
    meta::collapse_depths::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};
    engine.open_window(t0);
    meta::collapse_depths::emit_previous(engine);
    const auto previous{engine.close_window(t1)};
    engine.open_window(t1);
    meta::collapse_depths::emit_current(engine);
    const auto current{engine.close_window(t2)};

    const auto band_floor_of{[](const meta::CubeBlock& cube) -> std::optional<std::uint64_t>
                             {
                                 for (const auto& axis : cube.axes)
                                     if (axis.name == "level")
                                         return axis.band_floor;
                                 return std::nullopt;
                             }};

    // (1) Did the mechanism run? The two windows must sit at DIFFERENT collapse depths, or this
    // pair is an ordinary same-depth diff wearing the name of a compare-at-min.
    ASSERT_TRUE(previous.has_cube);
    ASSERT_TRUE(current.has_cube);
    const auto previous_floor{band_floor_of(previous.cube)};
    const auto current_floor{band_floor_of(current.cube)};
    ASSERT_TRUE(previous_floor.has_value())
        << "the previous window must OVERRUN the cell budget so the level banding fires; it did "
           "not, so the two cubes share a collapse state and this pair proves nothing.\n"
           "  previous cell_count="
        << previous.cube.cell_count << " budget=" << previous.cube.cell_budget.value_or(0);
    EXPECT_FALSE(current_floor.has_value())
        << "the current window must stay UNDER the budget so its own axes carry no band_floor; it "
           "collapsed too (band_floor="
        << current_floor.value_or(0)
        << "), which makes both sides equal again.\n  current cell_count="
        << current.cube.cell_count << " budget=" << current.cube.cell_budget.value_or(0);

    const meta::MetaLogDiff delta{meta::diff(previous, current)};
    ASSERT_TRUE(delta.has_cube_diff);

    // (2) The diff is read at the COARSER of the two — the max band floor — so its axes equal the
    // COLLAPSED input's and not the un-collapsed one's. This is the §16.10 arithmetic, pinned by
    // value; it is also the whole falsifier for §13.6's unbolded equality comment.
    const auto diff_floor{[&delta]() -> std::optional<std::uint64_t>
                          {
                              for (const auto& axis : delta.cube_diff.axes)
                                  if (axis.name == "level")
                                      return axis.band_floor;
                              return std::nullopt;
                          }()};
    ASSERT_TRUE(diff_floor.has_value())
        << "compare-at-min takes the MAX band floor, so the diff must carry the collapsed input's "
           "stamp; it carried none, which reads the pair at a granularity one input never had";
    EXPECT_EQ(*diff_floor, *previous_floor) << "the diff must be read at the coarser side's floor ("
                                            << *previous_floor << "); got " << *diff_floor;
    EXPECT_NE(delta.cube_diff.axes, current.cube.axes)
        << "this pair exists precisely because the diff's axes CANNOT equal both inputs'; if they "
           "now equal the un-collapsed input's, the collapse stamp was lost";

    // (3) CONTAINMENT, which is the obligation that survives §13.6's non-normative equality
    // comment: every axis either input's cube declares must still be present in the diff, by name.
    // A diff that silently dropped an axis would describe a different space than its inputs.
    const auto names_of{[](const std::vector<meta::CubeAxis>& axes)
                        {
                            std::set<std::string> out;
                            for (const auto& axis : axes)
                                out.insert(axis.name);
                            return out;
                        }};
    const std::set<std::string> diff_names{names_of(delta.cube_diff.axes)};
    for (const auto& stored : {previous.cube.axes, current.cube.axes})
        for (const auto& name : names_of(stored))
            EXPECT_TRUE(diff_names.contains(name))
                << "an axis an input cube declares (" << name << ") is absent from cube_diff.axes";
}

// ── The differential-axis falsifier, held over BOTH pairs ─────────────────────────────
//
// A differential axis is EMERGENT-AT-DIFF: it has no stored-cube domain, its baseline projection
// is uniformly the mute star, and it is only ever pinned on the CURRENT side. Two consequences are
// document-local and therefore checkable without any cross-document oracle: a border cell pinning
// one MUST carry previous_count == 0, and it MUST appear under `emerging` — never `vanishing`.
// This is the falsifiable obligation that replaces §13.6's non-normative axes-equality comment.
//
// It is written as a predicate over the whole cube_diff rather than a lookup of the one cell the
// latency_shift scenario produces, so a SECOND differential axis added later is judged the day it
// appears rather than the day someone remembers to extend this. The identification predicate is
// CONTAINMENT — an axis in cube_diff.axes whose name neither input's cube declares — and not the
// axis's `kind`: `kind` is a value-shape discriminator the standard owns, and keying on it would
// make this arm decay silently the moment that spelling changes.
TEST(MetaLogDocument, ADifferentialAxisOnlyEverPinsAnEmergingCellFromZero)
{
    const auto check{[](std::string_view what, const meta::MetaLogDocument& previous,
                        const meta::MetaLogDocument& current, const meta::MetaLogDiff& delta)
                     {
                         std::set<std::string> stored;
                         for (const auto& axes : {previous.cube.axes, current.cube.axes})
                             for (const auto& axis : axes)
                                 stored.insert(axis.name);
                         std::set<std::string> differential;
                         for (const auto& axis : delta.cube_diff.axes)
                             if (!stored.contains(axis.name))
                                 differential.insert(axis.name);

                         std::size_t pinned{0};
                         const auto scan{
                             [&](const meta::CubeBorder& border, bool is_emerging)
                             {
                                 for (const auto* region : {&border.lower, &border.upper})
                                     for (const auto& cell : *region)
                                     {
                                         if (!cell.coord.latency_shift)
                                             continue;
                                         ++pinned;
                                         EXPECT_TRUE(is_emerging)
                                             << what
                                             << ": a cell pinning a differential axis "
                                                "appeared under `vanishing`; the "
                                                "baseline projection is uniformly mute, "
                                                "so it can only ever emerge";
                                         EXPECT_EQ(cell.previous_count, 0U)
                                             << what
                                             << ": a cell pinning a differential axis "
                                                "carries previous_count="
                                             << cell.previous_count
                                             << "; the axis has no stored-cube domain, "
                                                "so its baseline count is 0 by "
                                                "construction";
                                     }
                             }};
                         if (delta.has_cube_diff && delta.cube_diff.has_emerging)
                             scan(delta.cube_diff.emerging, true);
                         if (delta.has_cube_diff && delta.cube_diff.has_vanishing)
                             scan(delta.cube_diff.vanishing, false);
                         return std::pair{differential.size(), pinned};
                     }};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    // The pair that HAS a differential axis. Its counts are the positive population.
    meta::MetaLogConfig shift_cfg;
    meta::latency_shift::configure(shift_cfg);
    meta::MetaLogEngine shift_engine{shift_cfg};
    shift_engine.open_window(t0);
    meta::latency_shift::emit_window(shift_engine, meta::latency_shift::kPreviousLatencyMs);
    const auto shift_previous{shift_engine.close_window(t1)};
    shift_engine.open_window(t1);
    meta::latency_shift::emit_window(shift_engine, meta::latency_shift::kCurrentLatencyMs);
    const auto shift_current{shift_engine.close_window(t2)};
    const auto [shift_axes, shift_cells]{check("latency_shift pair", shift_previous, shift_current,
                                               meta::diff(shift_previous, shift_current))};
    EXPECT_EQ(shift_axes, 1U)
        << "exactly one axis in this diff should be declared by neither input's cube; got "
        << shift_axes << ". A zero here means the arms above assert over an empty set.";
    EXPECT_GT(shift_cells, 0U)
        << "the population is EMPTY: no border cell pins the differential axis, so both assertions "
           "above passed for free. That is the shape this test exists to refuse.";

    // The CONTROL: a pair with no differential axis at all. Its differential set must be empty —
    // without it, "the predicate found nothing" and "the predicate cannot find anything" are the
    // same green.
    meta::MetaLogConfig depth_cfg;
    meta::collapse_depths::configure(depth_cfg);
    meta::MetaLogEngine depth_engine{depth_cfg};
    depth_engine.open_window(t0);
    meta::collapse_depths::emit_previous(depth_engine);
    const auto depth_previous{depth_engine.close_window(t1)};
    depth_engine.open_window(t1);
    meta::collapse_depths::emit_current(depth_engine);
    const auto depth_current{depth_engine.close_window(t2)};
    const auto [depth_axes,
                depth_cells]{check("collapse_depths pair", depth_previous, depth_current,
                                   meta::diff(depth_previous, depth_current))};
    EXPECT_EQ(depth_axes, 0U)
        << "a pair with no latency drift must declare no axis its inputs do not; got "
        << depth_axes;
    EXPECT_EQ(depth_cells, 0U)
        << "a pair with no differential axis must pin no differential coordinate; got "
        << depth_cells;
}

} // namespace
// NOLINTEND
