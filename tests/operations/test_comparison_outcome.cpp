// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_comparison_outcome.cpp — SPEC §13.2 / §13.2.1: the `comparison_outcome` member every
// MetaLogDiff has been REQUIRED to carry since metalog-spec 0.10.0, and the WITNESS rule that
// decides which of its two values is honest.
//
// The rule in one line: `"changed"` obliges the document to carry at least one signal property that
// is NON-VACUOUS by that property's own `x-metalog-vacuous` declaration in
// `schema/metalog_diff.v0.schema.json`; `"unchanged"` forbids one. Vacuity is DECLARED per property
// and is not a function of JSON shape — the spec's own worked counter-examples are that
// `stability_score` is vacuous at ONE, `tail_delta` carries findings with no array at all, and a
// `cube_diff`'s `axes` is a descriptor that must never witness.
//
// ── WHY THIS FILE HAS TWO GROUPS, AND WHAT EACH FIXTURE HAS TO CONTROL ───────────────────────────
// They prove two different things and neither can prove the other's.
//
// `ComparisonOutcomeRule` builds MetaLogDiff values BY HAND. The property under test is
// per-property vacuity, so the fixture must be able to move ONE signal property while holding all
// ten others at their declared vacuous value — which no pair of documents can do, because a real
// change moves several at once. Hand-built diffs are the only fixture with that control, and they
// are what makes each clause's failure attributable to the declaration it mirrors.
//
// `ComparisonOutcomeProducer` diffs ENGINE-PRODUCED documents. The property under test is that this
// producer actually LANDS on the declared vacuous values when it found nothing — an obligation
// §13.2.1 puts on the producer ("a value merely CLOSE to the declared vacuous value is a witness,
// and will be read as one"). A hand-built diff cannot show that: it asserts the arithmetic it was
// handed. Only two real windows through the real engine can.
//
// Every case in the first group is one that a SHAPE rule — "a non-empty array, or a scalar above
// zero" — decides differently from the declarations. That is deliberate: a suite whose cases agree
// under both rules would pass against the behaviour this file exists to replace.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using meta::ComparisonOutcome;

// ── Group A: the rule, one case per `x-metalog-vacuous` declaration ──────────────────────────────

// A diff carrying only §13.2's four REQUIRED members and no signal property at all. Every case
// below starts here and moves exactly one property off its declared vacuous value.
[[nodiscard]] meta::MetaLogDiff bare_diff()
{
    meta::MetaLogDiff diff;
    diff.previous.window_start_iso = "2026-01-01T00:00:00Z";
    diff.previous.window_end_iso = "2026-01-01T00:01:00Z";
    diff.current.window_start_iso = "2026-01-01T00:01:00Z";
    diff.current.window_end_iso = "2026-01-01T00:02:00Z";
    return diff;
}

// Verbose on failure: gtest cannot print a scoped enum, and "expected 0, got 1" is not a
// diagnosis. Reports the two WIRE TOKENS, which is what the document carries.
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

// `kl_divergence` / `js_divergence` declare `const: 0` — EXACT numeric equality. The spec names the
// failure this catches by value: "a divergence computed in floating point and serialised as 1e-17
// between two identical distributions is a FALSE WITNESS, and the defect is the producer's". The
// rule side of that is this: 1e-17 IS a witness, so a producer must never emit it for "no change".
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

    diff.kl_divergence = -0.0; // `const: 0` matches -0.0: same number, not a witness
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));
}

// `stability_score` declares `const: 1`, NOT 0 — it is `1 - js_divergence`. A "greater than zero"
// shape rule fires on PERFECT STABILITY and is silent nowhere; this asserts the opposite polarity.
TEST(ComparisonOutcomeRule, StabilityScoreIsVacuousAtOneNotAtZero)
{
    auto diff{bare_diff()};

    diff.stability_score = 1.0;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    diff.stability_score = 0.0; // total instability — the value a shape rule reads as "no finding"
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));

    diff.stability_score = 0.9999999999999999;
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
}

// `template_deltas` declares PER ROW (`items.properties.delta.const: 0`) because the array is a
// union over both windows and is non-empty whenever either window is. A row is not a finding; a
// non-zero `delta` is. A `minItems: 1` shape rule would witness on all three rows below.
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

// `branching_delta` declares `maxItems: 0` — the ARRAY is the finding, so any row witnesses, even a
// row whose entropy did not move. This is the declaration that forces the producer-side rule proved
// in ProducerEmitsNoBranchingDeltaWhenNoEntropyMoved below: this array is a union too, so emitting
// it unconditionally would witness "changed" on two identical documents.
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

// `ngram_delta`'s declaration mutes `ngram_size` explicitly: it is a PARAMETER, never a finding.
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

// `tail_delta` has nine numeric members and NO array, so a "non-empty array" shape rule witnesses
// NEVER and a tail-shape change is rejected while being correct. The declaration names the three
// `*_delta` members; the six `previous_`/`current_` members are the coordinates they are taken
// between, and are loud and non-zero in both halves of this case.
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

// `cube_diff.axes` is the diff's declared COORDINATE SPACE — required, non-empty, and a descriptor
// (SPEC §13.6). Under a shape rule this was the single largest vacuity in the release: this
// producer emits a cube_diff whenever both inputs carry a cube, so EVERY such document would have
// witnessed "changed" for free. The finding is a populated emerging/vanishing border.
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

// `withheld_signals` (§13.2.2, new in 0.10.0) declares `maxItems: 0` — any name is a finding, the
// finding being that ANOTHER property carries one this document does not show. It is the only
// clause whose input is a property that never reaches the wire: this producer computes
// `field_histogram_deltas` on every diff and never serialises it (§3.5.2 blesses that), so a
// comparison whose ONLY finding lay there had, before 0.10.0, no honest outcome — `"changed"`
// carried no witness and `"unchanged"` was false. This producer emitted the false one, and the
// first EXPECT below is the one that fails against that behaviour.
TEST(ComparisonOutcomeRule, WithheldFieldHistogramDeltasWitnessThroughWithheldSignals)
{
    auto diff{bare_diff()};
    EXPECT_TRUE(meta::withheld_signals_of(diff).empty());
    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Unchanged));

    // One slot whose distribution moved, and nothing else in the whole document. Every other
    // signal property stays at its declared vacuous value, so this row is the sole finding — and
    // it is one no reader of the serialized document can see.
    meta::FieldHistogramDelta row;
    row.template_id = insight::template_id_of("user <*> logged in");
    row.param_index = 0;
    row.js_divergence = 0.42;
    diff.field_histogram_deltas.push_back(row);

    EXPECT_TRUE(outcome_is(diff, ComparisonOutcome::Changed));
    EXPECT_EQ(meta::withheld_signals_of(diff),
              (std::vector<std::string>{"field_histogram_deltas"}));
}

// §13.2.2 bounds what may be NAMED, and the bound is narrower than "everything this producer
// drops": a member must be in the witness set (§13.2.1 step 2) and must not already witness in the
// document. Two properties this producer computes fail that test in opposite ways, and naming
// either would be a false statement about what the document withholds — `ordinal_histogram_deltas`
// is not a member of the standard at all, and `service_edge_delta` IS carried, under the §7
// `extensions` container. Neither may appear, and neither may move the outcome on its own.
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

// ── Group B: the producer lands on the declared vacuous values ───────────────────────────────────

// One window's worth of events. `alpha` branches to `beta` and to `gamma`, so the closed document
// carries a non-empty `behavior.branching` — which is what makes the branching_delta case below a
// real test rather than a vacuous one.
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

// Two windows built by two engines from the SAME event stream: same counts, same branching, same
// cube, same line total. Every signal property must land on its declared vacuous value.
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

// The headline case. Two documents with identical content diff to a document that asserts
// "unchanged" AND carries no witness — each clause checked separately, because a single
// outcome assert would pass against a predicate that returned Unchanged unconditionally.
TEST(ComparisonOutcomeProducer, IdenticalWindowsAssertUnchangedAndCarryNoWitness)
{
    const auto [previous, current] = identical_pair();

    // Fixture preconditions. Without these the case below can pass by carrying nothing at all,
    // which is the shape of a gate that never looked.
    ASSERT_TRUE(previous.behavior.has_value() && previous.behavior->branching.has_value())
        << "fixture must produce a branching block, or the branching_delta clause is untested";
    ASSERT_FALSE(previous.behavior->branching->empty()) << "fixture branching block is empty";
    ASSERT_TRUE(previous.has_cube && current.has_cube)
        << "fixture must produce a cube, or the cube_diff clause is untested";
    ASSERT_EQ(previous.window.lines_observed, current.window.lines_observed)
        << "the two fixture windows are not identical: " << previous.window.lines_observed << " vs "
        << current.window.lines_observed << " lines";

    const auto diff{meta::diff(previous, current)};

    // The divergences must be EXACTLY their declared vacuous values, not merely close: `const: 0`
    // and `const: 1` are exact numeric equality, and 1e-17 is a false witness.
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

    // The `cube_diff`-only shape: this producer emits a cube_diff on every cube-bearing pair, so
    // its ORDINARY no-change output is `axes` and nothing else. That is a descriptor, not a
    // finding.
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

// The producer-side half of BranchingDeltaWitnessesOnAnyRowIncludingAZeroOne: the array is a union
// over both windows' branching maps, so before this rule it was non-empty on every pair of
// branching-bearing documents, identical or not.
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

// The other half of the rule: a real change must assert "changed", and the document must carry the
// witness that decided it — asserting the outcome alone would pass against a predicate that
// returned Changed unconditionally.
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

} // namespace
// NOLINTEND
