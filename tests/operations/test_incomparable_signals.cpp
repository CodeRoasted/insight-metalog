// refs: ADR-25.D11, F-SRC-metalog-spec:SPEC.md
// invariant: a diff states every comparison its PAIR made impossible, in incomparable_signals, and
// never leaves the property silently absent nor computes it across the impossibility.
// invariant: every arm reads the serialized diff, so the statement is checked on the wire bytes a
// consumer receives, not only on a member.
// invariant: the controls stay green before and after the rule: a comparable pair is compared, and
// a pair that holds no operand states nothing.
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace meta = insight::metalog;
using insight::LogLevel;
using insight::metalog::test::make_event;
using insight::metalog::test::ParamEvent;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT1{std::chrono::seconds{1700000060}};

// invariant: the knobs one arm turns; every other parameter is the engine's default, and no input
// carries a retention_profile, so each pair is the unstamped residual the rule exists for.
struct Knobs
{
    std::size_t ngram_size{2};
    std::size_t top_ngrams_size{meta::MetaLogConfig::kDefaultTopNgramsSize};
    std::size_t top_branching_size{meta::MetaLogConfig::kDefaultTopBranchingSize};
    std::size_t top_k_size{8};
    std::size_t reservoir_size{0};
    std::size_t max_param_histograms{0};
};

[[nodiscard]] meta::MetaLogConfig config_of(const Knobs& knobs)
{
    return meta::MetaLogConfig{.top_k_size = knobs.top_k_size,
                               .reservoir_size = knobs.reservoir_size,
                               .ngram_size = knobs.ngram_size,
                               .top_ngrams_size = knobs.top_ngrams_size,
                               .emit_stability = false,
                               .top_branching_size = knobs.top_branching_size,
                               .max_param_histograms = knobs.max_param_histograms};
}

// post: one closed window over a branching walk -- alpha is followed by beta or gamma in turn -- so
// the transition graph has a node of fanout two at both orders.
[[nodiscard]] meta::MetaLogDocument close_branching_on(meta::MetaLogEngine& engine)
{
    constexpr int kCycles{6};
    engine.open_window(kT0);
    for (int cycle = 0; cycle < kCycles; ++cycle)
    {
        engine.ingest_event(make_event("walk alpha step"));
        engine.ingest_event(make_event("walk beta step"));
        engine.ingest_event(make_event("walk alpha step"));
        engine.ingest_event(make_event("walk gamma step"));
        engine.ingest_event(make_event("walk alpha step"));
        engine.ingest_event(make_event("walk beta step"));
    }
    return engine.close_window(kT1);
}

[[nodiscard]] meta::MetaLogDocument close_branching(const Knobs& knobs)
{
    meta::MetaLogEngine engine{config_of(knobs)};
    return close_branching_on(engine);
}

// post: one closed window whose two frequent templates fill top_k at size 2 and whose two rare
// failures fall below it, so a reservoir-keeping configuration retains them.
[[nodiscard]] meta::MetaLogDocument close_with_rare_failures(const Knobs& knobs)
{
    constexpr int kFrequent{10};
    meta::MetaLogEngine engine{config_of(knobs)};
    engine.open_window(kT0);
    for (int line = 0; line < kFrequent; ++line)
    {
        engine.ingest_event(make_event("served request ok"));
        engine.ingest_event(make_event("heartbeat ok"));
    }
    engine.ingest_event(make_event("disk controller failed", LogLevel::Fatal));
    engine.ingest_event(make_event("database timeout", LogLevel::Error));
    return engine.close_window(kT1);
}

// post: one closed window of a single template with one whole-token wildcard, under the knobs'
// histogram cap, so a tracking configuration builds one slot.
[[nodiscard]] meta::MetaLogDocument close_with_slot(const Knobs& knobs,
                                                    std::initializer_list<std::string_view> values)
{
    meta::MetaLogEngine engine{config_of(knobs)};
    engine.open_window(kT0);
    for (const std::string_view value : values)
    {
        const auto event{ParamEvent::make("GET <*>", {value})};
        engine.ingest_event(event.event);
    }
    return engine.close_window(kT1);
}

[[nodiscard]] std::string wire_of(const meta::MetaLogDiff& delta)
{
    return written_or_fail(meta::to_json(delta));
}

// post: how many times `needle` occurs in `haystack`, overlaps excluded.
[[nodiscard]] std::size_t occurrences(std::string_view haystack, std::string_view needle)
{
    std::size_t count{0};
    for (std::size_t at{haystack.find(needle)}; at != std::string_view::npos;
         at = haystack.find(needle, at + needle.size()))
        ++count;
    return count;
}

// post: the serialized incomparable_signals object, or an empty string when the diff has none.
// note: the object holds no string with a brace in it, so a depth count delimits it.
[[nodiscard]] std::string incomparable_member(std::string_view json)
{
    constexpr std::string_view kKey{R"("incomparable_signals":)"};
    const std::size_t key_at{json.find(kKey)};
    if (key_at == std::string_view::npos)
        return {};
    const std::size_t open_at{key_at + kKey.size()};
    int depth{0};
    for (std::size_t at{open_at}; at < json.size(); ++at)
    {
        if (json[at] == '{')
            ++depth;
        else if (json[at] == '}' && --depth == 0)
            return std::string{json.substr(open_at, at - open_at + 1)};
    }
    return std::string{json.substr(open_at)};
}

// post: the one statement keyed by `property` with an absent_in_one_input reason.
[[nodiscard]] std::string absent_statement(std::string_view property, std::string_view absent_in,
                                           std::string_view block)
{
    return std::format(R"("{}":{{"reason":"absent_in_one_input","absent_in":"{}","block":"{}"}})",
                       property, absent_in, block);
}

// post: the one statement keyed by `property` with an ngram_size_differs reason.
[[nodiscard]] std::string order_statement(std::string_view property, std::size_t previous_order,
                                          std::size_t current_order)
{
    return std::format(
        R"("{}":{{"reason":"ngram_size_differs","previous_ngram_size":{},"current_ngram_size":{}}})",
        property, previous_order, current_order);
}

// post: records a failure naming the diff unless `statement` sits inside its incomparable_signals.
void expect_stated(std::string_view name, std::string_view json, std::string_view statement)
{
    const std::string member{incomparable_member(json)};
    EXPECT_NE(member.find(statement), std::string::npos)
        << name << " does not state the comparison it could not make; expected\n  " << statement
        << "\ninside incomparable_signals, which reads\n  "
        << (member.empty() ? std::string{"<absent>"} : member) << "\nin\n  " << json;
}

// post: records a failure unless `property` occurs exactly once on the wire -- as the key of its
// statement, never also as the property the statement says was not computed.
void expect_named_once(std::string_view name, std::string_view json, std::string_view property)
{
    const std::string quoted{std::format(R"("{}")", property)};
    EXPECT_EQ(occurrences(json, quoted), 1U)
        << name << " names " << quoted << " " << occurrences(json, quoted)
        << " time(s); only incomparable_signals may name an incomparable property:\n  " << json;
}

} // namespace

// invariant: only one input carries behavior, so ngram_delta and branching_delta are both stated
// absent_in_one_input with the absent side in its role, block behavior.
TEST(IncomparableSignals, OneSidedBehaviorStatesNgramAndBranchingDeltas)
{
    const auto with_behavior{close_branching({})};
    const auto without_behavior{close_branching({.top_ngrams_size = 0})};
    ASSERT_TRUE(with_behavior.behavior.has_value())
        << "the present side must carry behavior, or there is no one-sided pair";
    ASSERT_FALSE(without_behavior.behavior.has_value())
        << "top_ngrams_size 0 must omit behavior, or the absent side is not absent";

    for (const auto& [name, json, absent_in] :
         {std::tuple{"diff(no behavior, behavior)",
                     wire_of(meta::diff(without_behavior, with_behavior)), "previous"},
          std::tuple{"diff(behavior, no behavior)",
                     wire_of(meta::diff(with_behavior, without_behavior)), "current"}})
    {
        expect_stated(name, json, absent_statement("ngram_delta", absent_in, "behavior"));
        expect_stated(name, json, absent_statement("branching_delta", absent_in, "behavior"));
        expect_named_once(name, json, "ngram_delta");
        expect_named_once(name, json, "branching_delta");
    }
}

// invariant: two orders over one identical workload, so branching_delta across them compares two
// different graphs and is stated ngram_size_differs beside ngram_delta.
TEST(IncomparableSignals, CrossOrderPairStatesBranchingDelta)
{
    const auto order2{close_branching({.ngram_size = 2})};
    const auto order3{close_branching({.ngram_size = 3})};
    ASSERT_TRUE(order2.behavior && order2.behavior->branching && order3.behavior &&
                order3.behavior->branching)
        << "both orders must carry a branching block, or the cross-order rows have no operand";

    for (const auto& [name, json, previous_order, current_order] :
         {std::tuple{"diff(order 2, order 3)", wire_of(meta::diff(order2, order3)), 2U, 3U},
          std::tuple{"diff(order 3, order 2)", wire_of(meta::diff(order3, order2)), 3U, 2U}})
    {
        expect_stated(name, json,
                      order_statement("branching_delta", previous_order, current_order));
        expect_stated(name, json, order_statement("ngram_delta", previous_order, current_order));
        expect_named_once(name, json, "branching_delta");
        expect_named_once(name, json, "ngram_delta");
    }
}

// invariant: both inputs carry behavior and exactly one carries a branching block, so only
// branching_delta is stated, block behavior.branching; ngram_delta is compared as usual.
TEST(IncomparableSignals, OneSidedBranchingStatesBranchingDeltaOnly)
{
    const auto with_branching{close_branching({})};
    const auto without_branching{close_branching({.top_branching_size = 0})};
    ASSERT_TRUE(without_branching.behavior.has_value());
    ASSERT_FALSE(without_branching.behavior->branching.has_value() &&
                 !without_branching.behavior->branching->empty())
        << "top_branching_size 0 must leave no branching rows";

    const std::string json{wire_of(meta::diff(without_branching, with_branching))};
    expect_stated("diff(no branching, branching)", json,
                  absent_statement("branching_delta", "previous", "behavior.branching"));
    EXPECT_EQ(incomparable_member(json).find(R"("ngram_delta")"), std::string::npos)
        << "both inputs carry behavior at one order, so ngram_delta is comparable:\n  " << json;
}

// invariant: exactly one input carries a cube, so cube_diff is stated absent_in_one_input, block
// cube, and no border is computed against nothing.
TEST(IncomparableSignals, OneSidedCubeStatesCubeDiff)
{
    const auto with_cube{close_branching({})};
    auto without_cube{close_branching({})};
    ASSERT_TRUE(with_cube.has_cube) << "every raw window carries a cube";
    without_cube.has_cube = false;
    without_cube.cube = {};

    for (const auto& [name, json, absent_in] :
         {std::tuple{"diff(no cube, cube)", wire_of(meta::diff(without_cube, with_cube)),
                     "previous"},
          std::tuple{"diff(cube, no cube)", wire_of(meta::diff(with_cube, without_cube)),
                     "current"}})
    {
        expect_stated(name, json, absent_statement("cube_diff", absent_in, "cube"));
        expect_named_once(name, json, "cube_diff");
    }
}

// invariant: two cubes whose axis SCHEMAS differ -- one axis renamed -- are stated
// cube_axes_differ with both inputs' axes, each descriptor carrying no collapse stamp.
TEST(IncomparableSignals, SchemaUnequalCubesStateCubeAxesDiffer)
{
    const auto previous{close_branching({})};
    auto current{close_branching({})};
    ASSERT_TRUE(previous.has_cube && current.has_cube);
    for (auto& axis : current.cube.axes)
        if (axis.name == "level")
            axis.name = "severity";

    const std::string json{wire_of(meta::diff(previous, current))};
    const std::string stated{R"("cube_diff":{"reason":"cube_axes_differ",)"
                             R"("previous_axes":[{"name":"level","kind":"categorical"},)"
                             R"({"name":"where","kind":"chain","chain":["component"]},)"
                             R"({"name":"structural_role","kind":"categorical"}],)"
                             R"("current_axes":[{"name":"severity","kind":"categorical"},)"
                             R"({"name":"where","kind":"chain","chain":["component"]},)"
                             R"({"name":"structural_role","kind":"categorical"}]})"};
    expect_stated("diff(level axis, severity axis)", json, stated);
    expect_named_once("diff(level axis, severity axis)", json, "cube_diff");
    EXPECT_EQ(incomparable_member(json).find("floor_depth"), std::string::npos)
        << "the evidence is the schema that differed; a collapse stamp in it points the reader at "
           "the wrong difference:\n  "
        << incomparable_member(json);
}

// invariant: one input keeps a reservoir and the other does not, so reservoir_delta is stated
// absent_in_one_input, block stats.reservoir, and no entry is reported born or dead.
TEST(IncomparableSignals, OneSidedReservoirStatesReservoirDelta)
{
    const auto keeping{close_with_rare_failures({.top_k_size = 2, .reservoir_size = 8})};
    const auto not_keeping{close_with_rare_failures({.top_k_size = 2})};
    ASSERT_FALSE(keeping.stats.reservoir.empty())
        << "the keeping side must retain its rare failures, or no false birth is reachable";

    for (const auto& [name, json, absent_in] :
         {std::tuple{"diff(no reservoir, reservoir)", wire_of(meta::diff(not_keeping, keeping)),
                     "previous"},
          std::tuple{"diff(reservoir, no reservoir)", wire_of(meta::diff(keeping, not_keeping)),
                     "current"}})
    {
        expect_stated(name, json,
                      absent_statement("reservoir_delta", absent_in, "stats.reservoir"));
        expect_named_once(name, json, "reservoir_delta");
        EXPECT_NE(json.find(R"("comparison_outcome":"unchanged")"), std::string::npos)
            << name << " asserts a change over ONE workload; configuration is not a birth:\n  "
            << json;
    }
}

// invariant: one input tracks histograms and the other does not, so field_histogram_deltas is
// stated absent_in_one_input, block stats.top_k[].param_histograms, and never withheld.
TEST(IncomparableSignals, OneSidedHistogramsStateFieldHistogramDeltas)
{
    const auto tracking{close_with_slot({.max_param_histograms = 4}, {"/a", "/b", "/a"})};
    const auto not_tracking{close_with_slot({}, {"/c", "/c", "/d"})};
    ASSERT_TRUE(tracking.stats.param_histograms_size.has_value())
        << "a tracking configuration declares its cap";

    for (const auto& [name, json, absent_in] :
         {std::tuple{"diff(untracked, tracked)", wire_of(meta::diff(not_tracking, tracking)),
                     "previous"},
          std::tuple{"diff(tracked, untracked)", wire_of(meta::diff(tracking, not_tracking)),
                     "current"}})
    {
        expect_stated(name, json,
                      absent_statement("field_histogram_deltas", absent_in,
                                       "stats.top_k[].param_histograms"));
        expect_named_once(name, json, "field_histogram_deltas");
    }
}

// invariant: both inputs track at different param_histograms_size, so field_histogram_deltas is
// stated histogram_caps_differ with all four caps, never withheld though the slots differ.
TEST(IncomparableSignals, UnequalHistogramCapsStateHistogramCapsDiffer)
{
    const auto at_four{close_with_slot({.max_param_histograms = 4}, {"/a", "/b", "/a"})};
    const auto at_eight{close_with_slot({.max_param_histograms = 8}, {"/c", "/c", "/d"})};

    const std::string json{wire_of(meta::diff(at_four, at_eight))};
    expect_stated("diff(cap 4, cap 8)", json,
                  R"("field_histogram_deltas":{"reason":"histogram_caps_differ",)"
                  R"("previous_param_histograms_size":4,"current_param_histograms_size":8,)"
                  R"("previous_value_counts_size":64,"current_value_counts_size":64})");
    expect_named_once("diff(cap 4, cap 8)", json, "field_histogram_deltas");
}

// invariant: a reservoir-keeping producer declares its cap in every document, an empty reservoir
// included, so the wire tells "kept and empty" from "not kept".
TEST(IncomparableSignals, ReservoirKeepingWindowDeclaresItsCapWhenEmpty)
{
    meta::MetaLogEngine engine{config_of({.reservoir_size = 8})};
    const auto doc{close_branching_on(engine)};
    ASSERT_TRUE(doc.stats.reservoir.empty())
        << "every template fits in top_k, so the reservoir must be empty for this arm to mean "
           "anything";
    const std::string json{written_or_fail(meta::to_json(doc, engine.registry()))};
    EXPECT_NE(json.find(R"("reservoir_size":8)"), std::string::npos)
        << "a window from a producer keeping a reservoir of 8 does not declare the cap, so its "
           "empty reservoir reads as no reservoir kept:\n  "
        << json;
    EXPECT_EQ(json.find(R"("reservoir":)"), std::string::npos)
        << "an empty reservoir is absent beside its declared cap:\n  " << json;
}

// invariant: CONTROL -- both inputs keep a reservoir and one retained nothing, so reservoir_delta
// is computed and nothing is stated incomparable.
TEST(IncomparableSignals, ControlEmptyReservoirBothKeepIsCompared)
{
    const auto empty{close_branching({.top_k_size = 2, .reservoir_size = 8})};
    const auto retaining{close_with_rare_failures({.top_k_size = 2, .reservoir_size = 8})};
    ASSERT_FALSE(retaining.stats.reservoir.empty());

    const std::string json{wire_of(meta::diff(empty, retaining))};
    EXPECT_NE(json.find(R"("reservoir_delta":{"new_salient":)"), std::string::npos)
        << "a salient single appearing beside a kept-and-empty reservoir is a true birth:\n  "
        << json;
    EXPECT_EQ(incomparable_member(json).find("reservoir_delta"), std::string::npos)
        << "both inputs keep a reservoir, so it is comparable:\n  " << json;
}

// invariant: CONTROL -- equal histogram caps compare row by row and state nothing.
TEST(IncomparableSignals, ControlEqualHistogramCapsAreCompared)
{
    const auto previous{close_with_slot({.max_param_histograms = 4}, {"/a", "/b", "/a"})};
    const auto current{close_with_slot({.max_param_histograms = 4}, {"/c", "/c", "/d"})};
    const auto delta{meta::diff(previous, current)};
    EXPECT_FALSE(delta.field_histogram_deltas.empty())
        << "one slot held on both sides at equal caps must yield a row";
    EXPECT_EQ(incomparable_member(wire_of(delta)), "")
        << "equal caps are comparable:\n  " << wire_of(delta);
}

// invariant: CONTROL -- two cubes of one schema at different collapse depths are compared at their
// minimal common collapse, which cube_diff.axes declares, and nothing is stated.
TEST(IncomparableSignals, ControlCollapseOnlyCubesAreCompared)
{
    const meta::MetaLogConfig cfg{.reservoir_size = 8, .emit_stability = false};
    const auto close_over{[&cfg](const std::vector<std::string>& components)
                          {
                              meta::MetaLogEngine engine{cfg};
                              engine.open_window(kT0);
                              for (const auto& component : components)
                                  for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug})
                                  {
                                      auto event{make_event("t", level)};
                                      event.component = component;
                                      engine.ingest_event(event);
                                  }
                              return engine.close_window(kT1);
                          }};
    constexpr int kQuietRepeats{3};
    constexpr int kBusyComponents{1500};
    const auto previous{close_over(std::vector<std::string>(kQuietRepeats, "auth"))};
    std::vector<std::string> busy;
    busy.reserve(kBusyComponents);
    for (int i = 0; i < kBusyComponents; ++i)
        busy.push_back("svc_" + std::to_string(i));
    const auto current{close_over(busy)};
    ASSERT_NE(previous.cube.axes, current.cube.axes)
        << "the budget collapse must stamp the busy window, or the pair is not collapse-only";

    const std::string json{wire_of(meta::diff(previous, current))};
    EXPECT_NE(json.find(R"("cube_diff":{"axes":[)"), std::string::npos)
        << "a collapse-only pair is comparable at its minimal common collapse:\n  " << json;
    EXPECT_EQ(incomparable_member(json), "") << "nothing is incomparable here:\n  " << json;
}

// invariant: CONTROL -- neither input carries behavior, so the pair holds no operand and nothing is
// stated: a member on every diff of a block-less producer would be noise.
TEST(IncomparableSignals, ControlNeitherBehaviorStatesNothing)
{
    const auto previous{close_branching({.top_ngrams_size = 0})};
    const auto current{close_branching({.top_ngrams_size = 0})};
    const std::string json{wire_of(meta::diff(previous, current))};
    EXPECT_EQ(incomparable_member(json), "")
        << "two inputs without behavior made no comparison impossible:\n  " << json;
}

// invariant: CONTROL -- the same-order pair with every block on both sides states nothing.
TEST(IncomparableSignals, ControlSameOrderPairStatesNothing)
{
    const std::string json{wire_of(meta::diff(close_branching({}), close_branching({})))};
    EXPECT_EQ(incomparable_member(json), "") << "a fully comparable pair:\n  " << json;
}

// invariant: the exported wire name of each of the four reasons is the token the serializer writes,
// so a consumer that spells a reason through to_string never drifts from the wire.
TEST(IncomparableSignals, ExportedReasonNameIsTheSerializedToken)
{
    const std::array<meta::IncomparableReason, 4> reasons{
        meta::IncomparableReason{
            meta::NGramSizeDiffers{.previous_ngram_size = 2, .current_ngram_size = 3}},
        meta::IncomparableReason{meta::AbsentInOneInput{.absent_in = meta::DiffSide::Current,
                                                        .block = meta::SignalBlock::Cube}},
        meta::IncomparableReason{meta::CubeAxesDiffer{}},
        meta::IncomparableReason{meta::HistogramCapsDiffer{.previous_param_histograms_size = 4,
                                                           .current_param_histograms_size = 8,
                                                           .previous_value_counts_size = 64,
                                                           .current_value_counts_size = 64}}};
    std::set<std::string_view> names;
    for (const meta::IncomparableReason& reason : reasons)
    {
        meta::MetaLogDiff delta;
        delta.incomparable_signals.cube_diff = reason;
        const std::string json{wire_of(delta)};
        const std::string token{
            std::format(R"("cube_diff":{{"reason":"{}")", meta::to_string(reason))};
        expect_stated(std::format("reason alternative {}", reason.index()), json, token);
        names.insert(meta::to_string(reason));
    }
    EXPECT_EQ(names.size(), reasons.size())
        << "two reason alternatives share one exported name; the vocabulary is closed and distinct";
}
