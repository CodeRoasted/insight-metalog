// NOLINTBEGIN — gate: literals and printed diagnostics are intended.
// test_retention_axis_census.cpp — every entry in a PRODUCED reservoir carries an ENGAGED
// `retention_axis`.
//
// THE PROPERTY, AND WHY IT IS A GATE RATHER THAN A NICETY. `ReservoirEntry::retention_axis` is a
// `std::optional<RetentionAxis>`, and its disengaged state is a CONTRACT: it encodes *no producer
// may name an axis it did not compute*, and it discharges that by making the fabrication
// unrepresentable. The guarantee that keeps it honest is that no producer ever emits the
// disengaged state — a fact about a CLOSED PRODUCER SET, not a property of the type. Exactly two
// sites in this package fill a reservoir entry, `close_window`'s candidate collection and
// `compose`'s reservoir re-derivation, and each admits a candidate only under
// `salience_score(...).score > 0`, which is the same predicate under which `SalienceVerdict::axis`
// is engaged.
//
// If that biconditional is ever broken — a third producer, a widened admission gate — the failure
// is SILENT and runs in the under-claiming direction: a consumer prints the un-attributed
// narrative ("retained by salience", no axis) for an entry that has a real axis, and no reader can
// tell that from an entry that genuinely has none. The declaration says so; nothing asserted it.
//
// ── HOMING (Kleio's call) ─────────────────────────────────────────────────────────────────────
// UNIT, at the PRODUCER, in `insight-metalog`. The guarantee is the producer's; a consumer-side
// arm in `insight-eidos` would prove only that the documents this test already builds happen to
// arrive intact, and the seam adds nothing to a property that is decided before the document is
// handed over.
//
// A FILE OF ITS OWN, not a fourth census loop inside `tests/reservoir/test_reservoir.cpp` and not
// a rider on `tests/operations/test_compose_algebra.cpp`. The subject spans BOTH producers, so it
// belongs to neither file's subject; splitting it in two would split one property across two
// suites and leave its expiry (below) declared in neither. `test_reservoir.cpp`'s three census
// loops assert `salience`, `structural_surprise`, `novelty` and `dominant_level` and never the
// axis; the POINTWISE halves live in `tests/stats/test_stats.cpp` (`score == 0 => !axis`, and four
// arms for `score > 0 => axis == <named>`). What no file joined is those halves to the ADMISSION
// path — which is exactly where a third producer would land, and it is the gap this file closes.
//
// ── ARMING CONDITIONS, ALL THREE MANDATORY ────────────────────────────────────────────────────
//   1. THE RESERVOIR IS ASSERTED NON-EMPTY FIRST. "Every entry has an axis" is vacuously true of
//      an empty reservoir, and a vacuous green here is indistinguishable from coverage —
//      empty-result is not empty-population.
//   2. BOTH PRODUCERS ARE COVERED. `close_window` and `compose` are two sites, and a gate on one
//      proves one.
//   3. THE CENSUS SEES MORE THAN ONE DISTINCT AXIS. A declared axis needs a distinct-value
//      assertion: a producer that stamped one constant on every entry would satisfy "engaged"
//      perfectly, and the field would be a decoration the census could not tell from a verdict.
//
// ── DECLARED EXPIRY, and its successor named at authoring time ────────────────────────────────
// THIS ARM'S VERDICT STOPS BEING CURRENT THE DAY AN INBOUND PATH EXISTS. `retention_axis` is
// domain-only and never serialised: nothing in this package deserialises a MetaLogDocument today
// (`to_json` exists, no inverse), which is what makes the producer set closed. A deserializer — a
// reader, a replayed fixture, a cross-process document — mints entries no salience computation
// produced, so the disengaged state becomes reachable BY CONSTRUCTION and this file's subject is
// wrong rather than merely failing.
//
// THE SUCCESSOR, spelled now so the re-scope is a repair and not a discovery: the property narrows
// from "every entry in a produced reservoir" to "every entry produced by `close_window` or
// `compose`" — same property, narrower subject. Concretely: the two arms below are unchanged (they
// already name their producer), and the file gains a third arm asserting that an entry arriving
// through the new inbound path is ALLOWED to be disengaged and is not fabricated into `Level`. Do
// not delete this file at that point; narrow its title sentence and add the arm.
//
// ── FALSIFIABILITY — OBSERVED 2026-08-30, then reverted (clause-7 discipline) ──────────────────
//   AX-A  `engine.cpp`'s reservoir admission stopped copying `sal.axis` onto the entry (the field
//         left default-constructed, i.e. disengaged): RED on the close_window arm with the
//         offending template ids and the whole census printed; the compose arm stayed GREEN —
//         which is the exact partition arming condition 2 exists for.
//   AX-B  `compose.cpp`'s `entry.retention_axis = cand.retention_axis` removed: RED on the compose
//         arm only.
//   Both mutations were applied, measured, and reverted; the tree at commit time is unmutated.
//
// Determinism: no RNG, no clock, no float comparison, no threads. Fixed event sequences, fixed
// window bounds (epoch + 60 s), integer salience bands throughout.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

/// The census result, carried so a failure can print the whole population rather than the first
/// offender: an axis contract is about EVERY entry, and a message naming one entry hides how far
/// the breakage went.
struct Census
{
    std::size_t entries{0};
    std::size_t disengaged{0};
    std::vector<std::string> rows;
    std::set<std::string> axes;
};

[[nodiscard]] Census census_of(const meta::MetaLogDocument& doc)
{
    Census out;
    for (const auto& entry : doc.stats.reservoir)
    {
        ++out.entries;
        const std::string axis{entry.retention_axis
                                   ? std::string{meta::to_string(*entry.retention_axis)}
                                   : std::string{"<DISENGAGED>"}};
        if (!entry.retention_axis)
            ++out.disengaged;
        else
            out.axes.insert(axis);
        out.rows.push_back("    template_id=" + insight::render(entry.template_id) +
                           " salience=" + std::to_string(entry.salience) +
                           " structural_surprise=" + std::to_string(entry.structural_surprise) +
                           " novelty=" + std::to_string(entry.novelty) + " axis=" + axis);
    }
    return out;
}

/// Named `render_census`, not `render`: canon exports `insight::render(TemplateId)` and a
/// same-named local overload in an unnamed namespace is the kind of resolution puzzle a reader
/// should never have to solve inside a diagnostic.
[[nodiscard]] std::string render_census(const Census& census)
{
    std::string out;
    for (const std::string& row : census.rows)
    {
        out += row;
        out += '\n';
    }
    return out.empty() ? std::string{"    <empty reservoir>\n"} : out;
}

/// The template vocabulary of one window. Every member is a STRING LITERAL with static storage:
/// `CanonicalEvent::template_str` is a `std::string_view`, so a template built from a temporary
/// `std::string` would leave the engine reading freed bytes — a fixture defect that would surface
/// as an unrelated failure somewhere downstream.
struct TemplateSet
{
    std::string_view steady_a;
    std::string_view steady_b;
    std::string_view steady_c;
    std::string_view off_path;
    std::string_view late;
    std::string_view rare_error;
};

// Two disjoint vocabularies, so the compose arm merges two DIFFERENT populations rather than
// composing a document with a copy of itself.
constexpr TemplateSet kFirstWindow{.steady_a = "alpha request received",
                                   .steady_b = "beta verify token",
                                   .steady_c = "gamma response sent",
                                   .off_path = "took alternate cache path",
                                   .late = "cache warmer started",
                                   .rare_error = "connection refused to db"};
constexpr TemplateSet kSecondWindow{.steady_a = "delta request accepted",
                                    .steady_b = "epsilon verify session",
                                    .steady_c = "zeta response flushed",
                                    .off_path = "took alternate replica path",
                                    .late = "prefetcher started",
                                    .rare_error = "connection reset by peer"};

/// One window carrying THREE distinct retention causes, so the census is not satisfiable by a
/// single stamped constant:
///   * a rare Error below top_k                                              -> the severity band
///   * a benign Info off the dominant path, reached only via a ~3% transition -> structure
///   * a benign Info emerging late and self-looping                          -> time (novelty)
/// The steady bed makes all three rare by frequency, so none of them rides top_k instead.
[[nodiscard]] meta::MetaLogDocument close_window_with_three_causes(std::size_t top_k,
                                                                   std::size_t reservoir_size,
                                                                   const TemplateSet& templates)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = top_k, .reservoir_size = reservoir_size, .emit_stability = false}};
    engine.open_window(std::chrono::system_clock::time_point{});
    for (int rep = 0; rep < 100; ++rep)
    {
        engine.ingest_event(make_event(templates.steady_a));
        engine.ingest_event(make_event(templates.steady_b));
        engine.ingest_event(make_event(templates.steady_c));
    }
    // Structure: a rare recurring off-path branch B -> X -> C.
    for (int rep = 0; rep < 3; ++rep)
    {
        engine.ingest_event(make_event(templates.steady_a));
        engine.ingest_event(make_event(templates.steady_b));
        engine.ingest_event(make_event(templates.off_path, insight::LogLevel::Info));
        engine.ingest_event(make_event(templates.steady_c));
    }
    // Time: a benign self-looping template that only starts near the end.
    for (int rep = 0; rep < 5; ++rep)
        engine.ingest_event(make_event(templates.late, insight::LogLevel::Info));
    // Severity: one rare Error.
    engine.ingest_event(make_event(templates.rare_error, insight::LogLevel::Error));
    return engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60});
}
} // namespace

// ══ producer 1 — `close_window` ═══════════════════════════════════════════════════════════════
TEST(RetentionAxisCensus, EveryEntryClosedByCloseWindowCarriesAnEngagedAxis)
{
    const auto doc{close_window_with_three_causes(/*top_k=*/3, /*reservoir_size=*/8, kFirstWindow)};
    const Census census{census_of(doc)};

    // Arming condition 1 — the population, before any claim about it.
    ASSERT_GT(census.entries, 0U)
        << "the reservoir is EMPTY, so the census below would pass vacuously and prove nothing. "
           "Empty-result is not empty-population: fix the fixture, never the assertion.";

    EXPECT_EQ(census.disengaged, 0U)
        << census.disengaged << " of " << census.entries
        << " entries produced by close_window carry a DISENGAGED retention_axis. The producer set "
           "is supposed to be closed over `salience_score(...).score > 0`, which is exactly when "
           "the verdict's axis is engaged — so either a new admission path skipped the stamp, or "
           "the biconditional in salience_score moved. Census:\n"
        << render_census(census);

    // Arming condition 3 — a declared axis needs a distinct-value assertion.
    EXPECT_GE(census.axes.size(), 2U)
        << "the census saw " << census.axes.size()
        << " distinct axis value(s): a producer stamping one constant on every entry would satisfy "
           "\"engaged\" perfectly and the field would carry no verdict. Census:\n"
        << render_census(census);
}

// ══ producer 2 — `compose` ════════════════════════════════════════════════════════════════════
//
// Composition re-derives salience over the MERGED counts (rarity is scale-relative), so this arm
// exercises a second, independent `salience_score` call site with its own admission gate — not the
// first producer's output travelling through. Both inputs are real engine documents: a hand-built
// pair would be exactly the fixture population whose entries legitimately have no axis, and the
// arm would then be measuring the fixture rather than the producer.
TEST(RetentionAxisCensus, EveryEntryRederivedByComposeCarriesAnEngagedAxis)
{
    const auto lhs{close_window_with_three_causes(/*top_k=*/3, /*reservoir_size=*/8, kFirstWindow)};
    const auto rhs{
        close_window_with_three_causes(/*top_k=*/3, /*reservoir_size=*/8, kSecondWindow)};
    ASSERT_FALSE(lhs.stats.reservoir.empty()) << "the left input carries no reservoir to compose";
    ASSERT_FALSE(rhs.stats.reservoir.empty()) << "the right input carries no reservoir to compose";

    const auto composed{meta::compose(lhs, rhs)};
    const Census census{census_of(composed)};

    ASSERT_GT(census.entries, 0U)
        << "the COMPOSED reservoir is empty, so this arm would pass vacuously. The two inputs "
           "carried "
        << lhs.stats.reservoir.size() << " and " << rhs.stats.reservoir.size()
        << " entries respectively.";

    EXPECT_EQ(census.disengaged, 0U)
        << census.disengaged << " of " << census.entries
        << " entries re-derived by compose carry a DISENGAGED retention_axis — compose's own "
           "`salience_score` call admits only `score > 0`, so an absent axis there means the "
           "verdict was dropped between the score and the entry. Census:\n"
        << render_census(census);

    EXPECT_GE(census.axes.size(), 2U)
        << "the composed census saw " << census.axes.size() << " distinct axis value(s). Census:\n"
        << render_census(census);
}
// NOLINTEND
