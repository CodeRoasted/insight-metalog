
// refs: F-SRC-metalog-spec:SPEC.md, F-SRC-metalog-spec:metalog_diff.v0.schema.json
// invariant: every MetaLogDiff must carry comparison_outcome, and the witness rule decides which of
// its two values is honest.
// invariant: changed obliges the document to carry at least one signal property that is NON-VACUOUS
// by that property's own x-metalog-vacuous declaration; unchanged forbids one.
// invariant: vacuity is DECLARED per property and is not a function of JSON shape.
// note: stability_score is vacuous at ONE and tail_delta has findings with no array at all.
// invariant: the diff schema declares x-metalog-vacuous on THIRTEEN top-level properties, and
// group A moves ONE while holding the other twelve at their declared vacuous value.
// note: no pair of real documents can do that, a real change moving several properties at once.
// invariant: group B diffs ENGINE-PRODUCED documents, because only two real windows can show the
// producer LANDS on the declared vacuous values; a value merely CLOSE to one is a witness.
// note: every group A case is one a SHAPE rule would decide differently from the declarations.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using meta::ComparisonOutcome;

// note: group A -- one case per x-metalog-vacuous declaration.
// invariant: the base diff carries only the four REQUIRED members and no signal property, so every
// case below moves exactly one property off its declared vacuous value.
[[nodiscard]] meta::MetaLogDiff bare_diff()
{
    meta::MetaLogDiff diff;
    diff.previous.window_start_iso = "2026-01-01T00:00:00Z";
    diff.previous.window_end_iso = "2026-01-01T00:01:00Z";
    diff.current.window_start_iso = "2026-01-01T00:01:00Z";
    diff.current.window_end_iso = "2026-01-01T00:02:00Z";
    return diff;
}

// invariant: gtest cannot print a scoped enum, so the report names the two WIRE TOKENS, which is
// what the document actually carries.
[[nodiscard]] ::testing::AssertionResult outcome_is(const meta::MetaLogDiff& diff,
                                                    ComparisonOutcome want)
{
    const ComparisonOutcome got{meta::comparison_outcome_of(diff)};
    if (got == want)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "comparison_outcome: expected \"" << meta::to_string(want) << "\", got \""
           << meta::to_string(got) << '"';
}

TEST(ComparisonOutcomeRule, NoSignalPropertyAtAllIsUnchanged)
{
    EXPECT_TRUE(outcome_is(bare_diff(), ComparisonOutcome::Unchanged));
}

// invariant: the divergences declare const 0, EXACT numeric equality, so a divergence serialised as
// 1e-17 between two identical distributions IS a witness and the defect is the producer's.
TEST(ComparisonOutcomeRule, DivergenceIsVacuousAtExactlyZeroAndWitnessesAtOneUlp)
{
    auto diff{bare_diff()};

    diff.kl_divergence = 0.0;
    diff.js_divergence = 0.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.js_divergence = 1e-17;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    diff.js_divergence = 0.0;
    diff.kl_divergence = 1e-17;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    diff.kl_divergence = -0.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));
}

// invariant: stability_score declares const 1 and not 0, being 1 minus js_divergence, so a
// greater-than-zero shape rule fires on PERFECT STABILITY and is silent nowhere.
TEST(ComparisonOutcomeRule, StabilityScoreIsVacuousAtOneNotAtZero)
{
    auto diff{bare_diff()};

    diff.stability_score = 1.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.stability_score = 0.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    diff.stability_score = 0.9999999999999999;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: template_deltas declares vacuity PER ROW because the array is a union over both
// windows and is non-empty whenever either is; a row is not a finding, a non-zero delta is.
TEST(ComparisonOutcomeRule, TemplateDeltasWitnessOnANonZeroDeltaNotOnARow)
{
    auto diff{bare_diff()};
    for (const char* tmpl : {"alpha <*>", "beta <*>", "gamma <*>"})
        diff.template_deltas.push_back(
            meta::TemplateDelta{.template_id = insight::template_id_of(tmpl),
                                .previous_count = 40,
                                .current_count = 40,
                                .delta = 0});
    ASSERT_EQ(diff.template_deltas.size(), 3U);
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.template_deltas[1].current_count = 41;
    diff.template_deltas[1].delta = 1;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: branching_delta declares maxItems 0, so the ARRAY is the finding and any row witnesses
// even one whose entropy did not move.
// note: it is a union too, so emitting it unconditionally would witness on two identical documents.
TEST(ComparisonOutcomeRule, BranchingDeltaWitnessesOnAnyRowIncludingAZeroOne)
{
    auto diff{bare_diff()};
    diff.branching_delta.push_back(
        meta::BranchingDelta{.template_id = insight::template_id_of("alpha <*>"),
                             .previous_entropy_bits = 1.5,
                             .current_entropy_bits = 1.5,
                             .delta_bits = 0.0});
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

TEST(ComparisonOutcomeRule, NewAndVanishedTemplatesWitnessOnAnyEntry)
{
    auto diff{bare_diff()};
    diff.new_templates.push_back(insight::template_id_of("alpha <*>"));
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    diff.new_templates.clear();
    diff.vanished_templates.push_back(insight::template_id_of("beta <*>"));
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: ngram_delta mutes ngram_size explicitly -- it is a PARAMETER, never a finding.
TEST(ComparisonOutcomeRule, NgramDeltaIsVacuousOnItsSizeParameterAlone)
{
    auto diff{bare_diff()};
    meta::NGramDelta ngram;
    ngram.ngram_size = 2;
    diff.ngram_delta = ngram;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.ngram_delta->new_ngrams.push_back(
        {insight::template_id_of("alpha <*>"), insight::template_id_of("beta <*>")});
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: tail_delta has nine numeric members and NO array, so a non-empty-array shape rule
// witnesses NEVER and rejects a tail-shape change while being correct.
// note: the six previous_/current_ members are the coordinates the three deltas are taken between.
TEST(ComparisonOutcomeRule, TailDeltaWitnessesOnADeltaNotOnItsCoordinates)
{
    auto diff{bare_diff()};
    meta::TailDelta tail;
    tail.previous_tail_template_count = 40;
    tail.current_tail_template_count = 40;
    tail.previous_tail_entropy_bits = 4.0;
    tail.current_tail_entropy_bits = 4.0;
    tail.previous_tail_max_rate = 0.001;
    tail.current_tail_max_rate = 0.001;
    diff.tail_delta = tail;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.tail_delta->current_tail_entropy_bits = 1.0;
    diff.tail_delta->tail_entropy_bits_delta = -3.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: cube_diff.axes is the diff's declared COORDINATE SPACE -- required, non-empty, and a
// descriptor -- so under a shape rule every cube-bearing document would witness changed for free.
// note: the finding is a populated emerging or vanishing border, never the axes.
TEST(ComparisonOutcomeRule, CubeDiffIsVacuousOnAxesAloneAndWitnessesOnABorderCell)
{
    auto diff{bare_diff()};
    diff.has_cube_diff = true;
    diff.cube_diff.axes.push_back(meta::CubeAxis{.name = "level", .kind = "categorical"});
    diff.cube_diff.axes.push_back(meta::CubeAxis{.name = "where", .kind = "chain"});
    ASSERT_FALSE(diff.cube_diff.axes.empty()) << "axes must be non-empty for this case to bite";
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    meta::CubeBorderCell cell;
    cell.coord.where = std::vector<std::string>{"db"};
    cell.previous_count = 0;
    cell.current_count = 57;
    diff.cube_diff.emerging.upper.push_back(cell);
    diff.cube_diff.has_emerging = true;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

TEST(ComparisonOutcomeRule, ReservoirDeltaWitnessesOnAnyOfItsThreeLists)
{
    auto diff{bare_diff()};
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    meta::ReservoirDeltaEntry entry;
    entry.template_id = insight::template_id_of("fatal <*>");
    entry.salience = 87;
    entry.count = 3;
    diff.reservoir_delta.new_salient.push_back(entry);
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// invariant: withheld_signals declares maxItems 0, so any name is a finding, the finding being that
// ANOTHER property carries one this document does not show.
// invariant: it is the only clause whose input never reaches the wire, this producer computing
// field_histogram_deltas on every diff and never serialising it.
// note: so a comparison whose only finding lay there once had no honest outcome available.
TEST(ComparisonOutcomeRule, WithheldFieldHistogramDeltasWitnessThroughWithheldSignals)
{
    auto diff{bare_diff()};
    EXPECT_TRUE(meta::withheld_signals_of(diff).empty());
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    // invariant: one slot whose distribution moved and nothing else in the document, so this row is
    // the sole finding -- and one no reader of the serialized document can see.
    meta::FieldHistogramDelta row;
    row.template_id = insight::template_id_of("user <*> logged in");
    row.param_index = 0;
    row.js_divergence = 0.42;
    diff.field_histogram_deltas.push_back(row);

    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
    EXPECT_EQ(meta::withheld_signals_of(diff),
              (std::vector<std::string>{"field_histogram_deltas"}));
}

// invariant: a row whose divergence, entropy and cardinality agree on both sides carries no
// movement, so it withholds nothing even when its sample counts differ.
// invariant: each of the three moving alone makes the row a finding the document withholds.
TEST(ComparisonOutcomeRule, AnUnmovedFieldHistogramRowWithholdsNothingAndEachMovementWithholds)
{
    meta::FieldHistogramDelta unmoved;
    unmoved.template_id = insight::template_id_of("user <*> logged in");
    unmoved.param_index = 0;
    unmoved.js_divergence = 0.0;
    unmoved.previous_entropy_bits = 1.0;
    unmoved.current_entropy_bits = 1.0;
    unmoved.previous_sample_count = 8;
    unmoved.current_sample_count = 16;
    unmoved.previous_cardinality = 2;
    unmoved.current_cardinality = 2;
    unmoved.cardinality_delta = 0;

    auto diff{bare_diff()};
    diff.field_histogram_deltas.push_back(unmoved);
    EXPECT_TRUE(meta::withheld_signals_of(diff).empty())
        << "a row that carries no movement was named withheld; a sample-count change alone is "
           "template_deltas' finding, not this property's";
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    auto diverged{unmoved};
    diverged.js_divergence = 0.01;
    auto entropy_moved{unmoved};
    entropy_moved.current_entropy_bits = 1.5;
    auto cardinality_moved{unmoved};
    cardinality_moved.current_cardinality = 3;
    cardinality_moved.cardinality_delta = 1;
    for (const auto& [movement, row] :
         {std::pair{std::string_view{"js_divergence"}, diverged},
          std::pair{std::string_view{"entropy_bits"}, entropy_moved},
          std::pair{std::string_view{"cardinality_delta"}, cardinality_moved}})
    {
        auto moved{bare_diff()};
        moved.field_histogram_deltas.push_back(unmoved);
        moved.field_histogram_deltas.push_back(row);
        EXPECT_EQ(meta::withheld_signals_of(moved),
                  (std::vector<std::string>{"field_histogram_deltas"}))
            << movement << " moved alone and was not named withheld";
        EXPECT_TRUE(outcome_is(moved, ComparisonOutcome::Changed))
            << movement << " moved alone and the outcome was not changed";
    }
}

// invariant: what may be NAMED is narrower than everything this producer drops: a member must be in
// the witness set and must not already witness in the document.
// note: one candidate is not in the standard at all and the other IS carried, under extensions.
TEST(ComparisonOutcomeRule, WithheldSignalsNamesNeitherANonStandardNorAnExtensionProperty)
{
    auto diff{bare_diff()};

    meta::OrdinalHistogramDelta ordinal;
    ordinal.template_id = insight::template_id_of("request took <*> ms");
    diff.ordinal_histogram_deltas.push_back(ordinal);

    meta::ServiceEdgeDelta edges;
    edges.emerged.push_back(meta::ServiceEdge{.caller = "api", .callee = "db", .weight = 7});
    diff.service_edge_delta = edges;

    EXPECT_TRUE(meta::withheld_signals_of(diff).empty());
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));
}

// note: group B -- the producer lands on the declared vacuous values.
// invariant: alpha branches to beta and to gamma, so the closed document carries a non-empty
// branching block, which is what makes the branching_delta case below a real test.
void ingest_branching_window(meta::MetaLogEngine& engine)
{
    auto alpha{make_event("alpha <*>")};
    auto beta{make_event("beta <*>")};
    auto gamma{make_event("gamma <*>", insight::LogLevel::Error)};
    for (int i = 0; i < 12; ++i)
    {
        engine.ingest_event(alpha);
        engine.ingest_event(beta);
        engine.ingest_event(alpha);
        engine.ingest_event(gamma);
    }
}

// invariant: two windows built by two engines from the SAME event stream -- same counts, same
// branching, same cube, same line total -- so every signal property must land on its vacuous value.
[[nodiscard]] std::pair<meta::MetaLogDocument, meta::MetaLogDocument> identical_pair()
{
    const auto t0{insight::Timestamp{} + std::chrono::hours{1}};
    const auto t1{t0 + std::chrono::seconds{60}};
    const auto t2{t1 + std::chrono::seconds{60}};

    meta::MetaLogEngine first;
    first.open_window(t0);
    ingest_branching_window(first);
    auto previous{first.close_window(t1)};

    meta::MetaLogEngine second;
    second.open_window(t1);
    ingest_branching_window(second);
    auto current{second.close_window(t2)};

    return {std::move(previous), std::move(current)};
}

// invariant: the headline case: identical content must diff to a document asserting unchanged AND
// carrying no witness, each clause checked separately.
// note: a single outcome assert would pass against a predicate returning Unchanged always.
TEST(ComparisonOutcomeProducer, IdenticalWindowsAssertUnchangedAndCarryNoWitness)
{
    const auto [previous, current] = identical_pair();

    // pre: without these the case can pass by carrying nothing at all, which is the shape of a gate
    // that never looked.
    ASSERT_TRUE(previous.behavior.has_value() && previous.behavior->branching.has_value())
        << "fixture must produce a branching block, or the branching_delta clause is untested";
    ASSERT_FALSE(previous.behavior->branching->empty()) << "fixture branching block is empty";
    ASSERT_TRUE(previous.has_cube && current.has_cube)
        << "fixture must produce a cube, or the cube_diff clause is untested";
    ASSERT_EQ(previous.window.lines_observed, current.window.lines_observed)
        << "the two fixture windows are not identical: " << previous.window.lines_observed << " vs "
        << current.window.lines_observed << " lines";

    const auto diff{meta::diff(previous, current)};

    // invariant: the divergences must be EXACTLY their declared vacuous values and not merely
    // close, because const 0 and const 1 are exact numeric equality and 1e-17 is a false witness.
    ASSERT_TRUE(diff.kl_divergence.has_value());
    ASSERT_TRUE(diff.js_divergence.has_value());
    ASSERT_TRUE(diff.stability_score.has_value());
    EXPECT_EQ(*diff.kl_divergence, 0.0)
        << "kl_divergence must be EXACTLY 0 on identical distributions; got "
        << std::format("{:.17g}", *diff.kl_divergence);
    EXPECT_EQ(*diff.js_divergence, 0.0)
        << "js_divergence must be EXACTLY 0 on identical distributions; got "
        << std::format("{:.17g}", *diff.js_divergence);
    EXPECT_EQ(*diff.stability_score, 1.0)
        << "stability_score is 1 - js_divergence, so its vacuous value is EXACTLY 1; got "
        << std::format("{:.17g}", *diff.stability_score);

    for (const auto& row : diff.template_deltas)
        EXPECT_EQ(row.delta, 0) << "template " << insight::render(row.template_id) << " moved by "
                                << row.delta << " between two identical windows";
    EXPECT_TRUE(diff.new_templates.empty()) << diff.new_templates.size() << " new template(s)";
    EXPECT_TRUE(diff.vanished_templates.empty())
        << diff.vanished_templates.size() << " vanished template(s)";
    EXPECT_TRUE(diff.branching_delta.empty())
        << "branching_delta carries " << diff.branching_delta.size()
        << " row(s) between two identical windows; its declared vacuous value is the EMPTY array "
           "(maxItems: 0), so any row is a FALSE WITNESS forcing comparison_outcome=changed";
    EXPECT_FALSE(diff.ngram_delta.has_value())
        << "ngram_delta engaged between two identical windows";
    EXPECT_TRUE(diff.reservoir_delta.empty()) << "reservoir_delta non-empty on an identical pair";
    if (diff.tail_delta)
    {
        EXPECT_EQ(diff.tail_delta->tail_template_count_delta, 0);
        EXPECT_EQ(diff.tail_delta->tail_entropy_bits_delta, 0.0);
        EXPECT_EQ(diff.tail_delta->tail_max_rate_delta, 0.0);
    }

    // invariant: this producer emits a cube_diff on every cube-bearing pair, so its ordinary
    // no-change output is axes and nothing else -- a descriptor, not a finding.
    ASSERT_TRUE(diff.has_cube_diff) << "both inputs carried a cube, so a cube_diff is owed";
    EXPECT_FALSE(diff.cube_diff.axes.empty()) << "a cube_diff owes its coordinate space";
    EXPECT_FALSE(diff.cube_diff.has_emerging) << "an emerging border between two identical windows";
    EXPECT_FALSE(diff.cube_diff.has_vanishing)
        << "a vanishing border between two identical windows";

    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    const std::string json{meta::to_json(diff)};
    EXPECT_NE(json.find(R"("comparison_outcome":"unchanged")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("cube_diff")"), std::string::npos)
        << "the cube_diff must still be published — it is vacuous, not absent:\n"
        << json;
    EXPECT_EQ(json.find(R"("branching_delta")"), std::string::npos)
        << "branching_delta must not reach the wire when no entropy moved:\n"
        << json;
}

// invariant: the producer-side half of the branching rule: the array is a union over both windows'
// branching maps, so before this rule it was non-empty on every branching-bearing pair.
TEST(ComparisonOutcomeProducer, EmitsNoBranchingDeltaWhenNoEntropyMoved)
{
    const auto [previous, current] = identical_pair();
    ASSERT_FALSE(previous.behavior->branching->empty());
    ASSERT_FALSE(current.behavior->branching->empty());

    const auto diff{meta::diff(previous, current)};
    EXPECT_TRUE(diff.branching_delta.empty())
        << "the two windows' branching entropies are identical, yet branching_delta carries "
        << diff.branching_delta.size() << " row(s)";
}

// invariant: a real change must assert changed AND the document must carry the witness that decided
// it, the outcome alone passing against a predicate that returns Changed always.
TEST(ComparisonOutcomeProducer, AChangedPairAssertsChangedAndCarriesTheWitness)
{
    const auto t0{insight::Timestamp{} + std::chrono::hours{1}};
    const auto t1{t0 + std::chrono::seconds{60}};
    const auto t2{t1 + std::chrono::seconds{60}};

    meta::MetaLogEngine first;
    first.open_window(t0);
    ingest_branching_window(first);
    const auto previous{first.close_window(t1)};

    meta::MetaLogEngine second;
    second.open_window(t1);
    ingest_branching_window(second);
    auto delta_ev{make_event("delta <*>", insight::LogLevel::Error)};
    for (int i = 0; i < 9; ++i)
        second.ingest_event(delta_ev);
    const auto current{second.close_window(t2)};

    const auto diff{meta::diff(previous, current)};

    ASSERT_FALSE(diff.new_templates.empty())
        << "fixture must introduce a template, or this case does not test a change";
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    const std::string json{meta::to_json(diff)};
    EXPECT_NE(json.find(R"("comparison_outcome":"changed")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("new_templates")"), std::string::npos)
        << "the witness that decided \"changed\" must be in the document:\n"
        << json;
}

// invariant: the withheld clause on the REAL producer: param histograms on, one parameterised
// template, the same events on both sides except the values `current_values` names.
// invariant: the template counts and sequence are equal on both sides, so every carried signal
// property lands on its vacuous value and the withheld clause alone decides the outcome.
[[nodiscard]] std::pair<meta::MetaLogDocument, meta::MetaLogDocument>
param_bearing_pair(std::span<const std::string_view> previous_values,
                   std::span<const std::string_view> current_values)
{
    const auto t0{insight::Timestamp{} + std::chrono::hours{1}};
    const auto t1{t0 + std::chrono::seconds{60}};
    const auto t2{t1 + std::chrono::seconds{60}};
    const meta::MetaLogConfig histograms_on{.max_param_histograms = 1};

    const auto window{[&](std::span<const std::string_view> values, insight::Timestamp open,
                          insight::Timestamp close)
                      {
                          meta::MetaLogEngine engine{histograms_on};
                          engine.open_window(open);
                          for (const std::string_view value : values)
                          {
                              const auto login{insight::metalog::test::ParamEvent::make(
                                  "user <*> logged in", {value})};
                              engine.ingest_event(login.event);
                          }
                          return engine.close_window(close);
                      }};
    return {window(previous_values, t0, t1), window(current_values, t1, t2)};
}

// post: whether the document's first top_k entry carries a field histogram.
[[nodiscard]] bool tracks_a_field_histogram(const meta::MetaLogDocument& document)
{
    return !document.stats.top_k.empty() && !document.stats.top_k.front().field_histograms.empty();
}

// post: one line per field-histogram row, for a failure message.
[[nodiscard]] std::string describe_field_histogram_rows(const meta::MetaLogDiff& diff)
{
    std::string rows;
    for (const auto& row : diff.field_histogram_deltas)
        rows +=
            std::format("\n  param {} js_divergence {:.17g} cardinality {} -> {}", row.param_index,
                        row.js_divergence, row.previous_cardinality, row.current_cardinality);
    return rows;
}

// invariant: identical histograms are not a finding, so a producer that computes them must still
// assert unchanged and name nothing withheld.
TEST(ComparisonOutcomeProducer, IdenticalParamHistogramsAssertUnchangedAndWithholdNothing)
{
    const std::array<std::string_view, 8> values{"alice", "bob", "alice", "bob",
                                                 "alice", "bob", "alice", "bob"};
    const auto [previous, current] = param_bearing_pair(values, values);

    // pre: without a histogram on both sides the withheld clause has nothing to read.
    ASSERT_TRUE(tracks_a_field_histogram(previous) && tracks_a_field_histogram(current))
        << "fixture must carry a field histogram on both sides, or the withheld clause is untested";

    const auto diff{meta::diff(previous, current)};
    const std::string rows{describe_field_histogram_rows(diff)};

    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged))
        << "two identical windows; field_histogram_deltas rows:" << rows;
    EXPECT_TRUE(meta::withheld_signals_of(diff).empty())
        << "a withheld signal named between two identical windows; rows:" << rows;
    const std::string json{meta::to_json(diff)};
    EXPECT_NE(json.find(R"("comparison_outcome":"unchanged")"), std::string::npos) << json;
    EXPECT_EQ(json.find(R"("withheld_signals")"), std::string::npos) << json;
}

// invariant: a moved value distribution is the finding the document cannot carry, so the real
// producer must assert changed AND name the withheld signal that decided it.
TEST(ComparisonOutcomeProducer, AMovedParamDistributionAloneAssertsChangedThroughTheWithheldSignal)
{
    const std::array<std::string_view, 8> previous_values{"alice", "bob", "alice", "bob",
                                                          "alice", "bob", "alice", "bob"};
    const std::array<std::string_view, 8> current_values{"alice", "carol", "alice", "carol",
                                                         "alice", "carol", "alice", "carol"};
    const auto [previous, current] = param_bearing_pair(previous_values, current_values);
    const auto diff{meta::diff(previous, current)};

    // pre: no carried property witnesses, so the outcome below is the withheld clause's alone.
    ASSERT_TRUE(diff.kl_divergence.has_value() && diff.js_divergence.has_value());
    ASSERT_EQ(*diff.kl_divergence, 0.0);
    ASSERT_EQ(*diff.js_divergence, 0.0);
    ASSERT_TRUE(diff.new_templates.empty() && diff.vanished_templates.empty());
    for (const auto& row : diff.template_deltas)
        ASSERT_EQ(row.delta, 0) << "template " << insight::render(row.template_id) << " moved";
    ASSERT_FALSE(diff.field_histogram_deltas.empty())
        << "the moved distribution produced no field histogram row";
    EXPECT_GT(diff.field_histogram_deltas.front().js_divergence, 0.0);

    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
    EXPECT_EQ(meta::withheld_signals_of(diff),
              (std::vector<std::string>{"field_histogram_deltas"}));
    const std::string json{meta::to_json(diff)};
    EXPECT_NE(json.find(R"("comparison_outcome":"changed")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("withheld_signals":["field_histogram_deltas"])"), std::string::npos)
        << json;
}

} // namespace
