// refs: ADR-31.D8, F-SRC-insight-metalog:golden.yaml
// invariant: there is NO committed golden hash here or anywhere -- determinism is proven by
// cross-leg AGREEMENT over 5 legs, emitted as a per-release artifact only.
// note: these tests keep the replayed scenarios non-hollow and pin derived VALUES, never bytes.
#include <gtest/gtest.h>

import insight.metalog.test;

// refs: F-SRC-insight-metalog:determinism_fixture.cpp
// invariant: every scenario header below is shared with the cross-leg fixture, so this coverage and
// the gate replay the identical window; the near-full arm is the M=128 admit/evict boundary.
#include "reservoir_nearfull_scenario.hpp"
// note: the second ADR-31.D8 arm, at the shipped streaming tuple, where the error RESERVE is live.
#include "reservoir_streaming_scenario.hpp"
// note: the over-cap top-K select, cut on a weight tie so the canonical-key tie-break decides.
#include "service_edges_overcap_scenario.hpp"
// note: the ONE window in the digest whose document carries behavior.dropped_ngram_observations.
#include "ngram_cap_scenario.hpp"
// note: the only scenario here whose replayed artifact is a MetaLogDiff and not a document.
#include "latency_shift_scenario.hpp"
// note: two cubes at different collapse depths.
#include "collapse_depths_scenario.hpp"

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// invariant: the near-full scenario must fill to the production M and its admit/evict boundary must
// be structural-surprise-driven, or the proof the gate replays goes hollow.
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

// refs: ADR-31.D8
// invariant: without this arm the determinism proof never runs at the tuple we deploy and the
// error-class reserve, live only here, is never driven at all.
// note: inverting the edge tie-break reds two counts while reservoir.size() holds: size is blind.
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

    // note: membership is read off interned template text, so a failure names a population.
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

    // refs: ADR-20.D7
    // invariant: with reservoir_error_reserve = 0 no error-class template survives the
    // over-subscribed band, so this count IS the reserve: it says admits and bounds at once.
    EXPECT_EQ(error_class, streaming::kErrorReserve)
        << "the error-class reserve is the ONLY reason a failure is retained in this window, so "
           "this count is the reserve itself. Anything else means the reserve stopped binding "
           "(more) or stopped admitting (fewer), and the shipped `e16` axis is no longer measured "
           "by anything.\n"
        << census();

    // invariant: 40 solid and 24 ambiguous share the band for 48 free slots, so at least 8
    // ambiguous must be admitted and at least 16 of the band must be rejected.
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

    // note: the general pool is filled by that band ALONE, which composes the two into one fact.
    EXPECT_EQ(solid + ambiguous, streaming::kGeneralSlots)
        << "a template from below the 8100 band reached the general pool, so the pool is no longer "
           "saturated by the solid/ambiguous tie group and the boundary has drifted off the "
           "equal-ratio spokes this arm exists to drive.\n"
        << census();
}

// refs: SRC-D-OTEL-21
// invariant: the emitted block must be OVER the cap and the cut must fall on a weight tie, so the
// last surviving edge is decided by the canonical-key tie-break alone.
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
    ASSERT_EQ(block.edges.size(), cfg.max_service_edges)
        << "the block must be capped at max_service_edges=" << cfg.max_service_edges << " (got "
        << block.edges.size() << ") — else the over-cap select path is not exercised";
    EXPECT_EQ(block.dropped_edges, 2U)
        << "5 distinct edges built, 3 kept → 2 dropped; a 0 here means the scenario went hollow";
    // invariant: {gateway,auth} beat {gateway,billing} and {worker,queue} at weight 2 on the
    // canonical key alone; a stdlib-order-dependent select surfaces a different 3rd edge.
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

// refs: F-SRC-metalog-spec:SPEC.md
// invariant: SPEC 4 reads an absent key in a 0.7.0+ document as an affirmative nothing-was-dropped,
// so a scenario that stopped binding would lose the coverage with nothing red.
// invariant: the count is DERIVED from the config in hand and the shipped literal is asserted
// beside it, because a derivation that agrees with itself proves nothing.
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

    // pre: the precondition, not the property: if the templates collapsed into fewer shapes the
    // bigram arithmetic below is arbitrary rather than wrong.
    ASSERT_EQ(doc.stats.unique_templates, meta::ngram_cap::kDistinctTemplates)
        << "each of the " << meta::ngram_cap::kDistinctTemplates
        << " one-shot strings must form its OWN template for the bigram count to hold; got "
        << doc.stats.unique_templates;

    // pre: the block must exist before its member is read; a block-absent document would satisfy a
    // naive not-what-I-expected reading while testing nothing.
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

    // note: the table filled to its bound, so the refusal count is a claim about the CAP.
    EXPECT_EQ(doc.behavior->top_ngrams.size(), cfg.top_ngrams_size)
        << "every admitted bigram is at count 1, so the top-N select runs entirely on its "
           "`sequence` tie-break over "
        << cfg.max_ngram_keys
        << " candidates — a tie-break that stopped being a total order surfaces as a cross-leg "
           "byte difference and nowhere else in this corpus";
}

// note: cube, WHERE and acquisition are unconditional; this pins values, never bytes.
TEST(MetaLogDocument, AlwaysOnCubeWhereAndAcquisitionFields)
{
    // invariant: component is a string_view INTO the event and the literals have static storage, so
    // the views outlive the test; an empty component is a free-text line carrying no WHERE.
    const auto ev = [](std::string_view tmpl, insight::LogLevel level, std::string_view component)
    {
        tok::CanonicalEvent e;
        e.template_str = tmpl;
        e.level = level;
        e.component = component;
        return e;
    };

    meta::MetaLogConfig cfg;
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    // note: window 1 is PARTIAL coverage over four components, one a tie broken ascending.
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

    // note: window 2 is a db ERROR burst over steady auth traffic, at FULL coverage.
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
    // note: dimension-metadata: the collapse guardrail's raw trigger inputs.
    EXPECT_EQ(doc1.acquisition->where_cardinality_per_depth,
              (std::vector<std::uint64_t>{doc1.acquisition->distinct_components}))
        << "depth-1 WHERE tree → one per-depth entry == distinct_components";
    EXPECT_EQ(doc1.acquisition->closed_cells, doc1.cube.cell_count)
        << "P_closed == the closed cube's cell count";
    // note: the mandatory cardinality signal: distinct count per cube axis.
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

// note: the W1 ordinal binned-carrier must be populated; the eidos W1 distance rides it.
TEST(MetaLogDocument, OrdinalCarrierPopulated)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 2;
    meta::MetaLogEngine engine{cfg};
    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};

    // note: a deterministic latency spread over several octaves on one template, ms to ns by x1e6.
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
        ev.ordinals = obs;
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

// refs: STU-3, F-SRC-insight-metalog:diff.cpp
// invariant: this guards a PAIR and not a window, because the artifact the cross-leg gate replays
// for it is a MetaLogDiff; two things can go hollow here independently.
// invariant: a drift under kShiftSampleFloor is projected away SILENTLY, so the
// did-the-mechanism-run assertion comes first, on the ordinal carrier both sides must carry.
// invariant: computed-but-dropped-at-the-DTO is live here, field_histogram_deltas reaching no wire,
// which is why the last assertion reads the JSON and not the struct.
// note: axes[].kind is deliberately NOT asserted -- that spelling is metalog-spec's to decide.
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

    // pre: both sides must carry a duration ladder on payments with at least kShiftSampleFloor
    // paired observations, or every assertion below is about a projected-away axis.
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
    constexpr std::uint64_t kShiftSampleFloor{32};
    ASSERT_GE(std::min(previous_total, current_total), kShiftSampleFloor)
        << "the shift is INADMISSIBLE below the floor and the axis is silently projected away, "
           "so this scenario would emit a plain 3-D border while still looking healthy.\n"
           "  payments duration observations: previous="
        << previous_total << " current=" << current_total << " floor=" << kShiftSampleFloor;

    const meta::MetaLogDiff delta{meta::diff(previous, current)};
    ASSERT_TRUE(delta.has_cube_diff)
        << "both windows carry a cube, so the pair must produce a cube_diff — without one the "
           "digest's --latency-shift section carries no differential axis at all";

    // invariant: the latency_shift axis is emergent-at-diff: no stored cube ever carries it, so its
    // presence here is the whole reason this section exists.
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

    // note: 100 ms to 100 s is 10 octaves on the frozen log2-ns ladder, deep inside HIGH.
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

    // note: the WIRE, not the struct: this separates engine computed one from digest carries one.
    const std::string wire{meta::to_json(delta)};
    EXPECT_NE(wire.find("\"cube_diff\""), std::string::npos)
        << "to_json(MetaLogDiff) dropped the cube_diff block — the domain object has it and the "
           "serialized artifact the gate judges does not.\n  wire: "
        << wire;
    EXPECT_NE(wire.find("\"latency_shift\""), std::string::npos)
        << "the differential axis reached no wire, so the digest section is decoration.\n  wire: "
        << wire;
}

// refs: F-SRC-metalog-spec:SPEC.md
// invariant: SPEC 16.10 reads a diff of two cubes at the COARSER of the two on each axis, and this
// pair refutes 13.6's unbolded axes-equality comment by construction.
// invariant: no equality arm is written -- it would pin a claim the standard does not make and
// would red the day 16.10 is exercised, which is here.
// note: it goes hollow if the previous window stops banding, so the collapse states are read FIRST.
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

    // pre: the two windows must sit at DIFFERENT collapse depths, or this pair is an ordinary
    // same-depth diff wearing the name of a compare-at-min.
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

    // invariant: compare-at-min takes the MAX band floor, so the diff's axes equal the COLLAPSED
    // input's and not the un-collapsed one's.
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

    // invariant: containment is the obligation that survives 13.6 -- every axis either input's cube
    // declares is still present in the diff, by name.
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

// invariant: a differential axis is emergent-at-diff, so a border cell pinning one carries
// previous_count == 0 and appears under emerging, never vanishing.
// invariant: the identification predicate is CONTAINMENT and never the axis kind: latency_shift is
// categorical exactly like level and structural_role, so kind cannot separate them at all.
// note: it is a predicate over the whole cube_diff, so a second such axis is judged as it appears.
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

    // note: the pair that HAS a differential axis; its counts are the positive population.
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

    // note: the CONTROL: without it, found nothing and cannot find anything are the same green.
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
