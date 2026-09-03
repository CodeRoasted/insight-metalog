// Unit tests: allow short identifiers and test-specific patterns.
//
// test_presence_churn_monoid.cpp — the DN-50.D4 presence-churn monoid and its fold through
// `compose()`.
//
// WHAT THE MEASURAND IS, once, because every assertion below is only meaningful against it. For a
// template over base windows w_1..w_n with presence p_i in {0,1}, churn is #{ i : p_i != p_{i+1} }.
// The composable element is C(B) = (first, transitions, indeterminate, last) and the product adds a
// boundary term that depends only on (last(A), first(B)). It answers "the presence of CONTENT
// oscillates" — a fact about the observed window range, computed on the document — and deliberately
// not "the set of detectors that fire oscillates", whose value is a function of every threshold
// upstream of it.
//
// WHERE THIS FILE STOPS, and who owns the rest. DN-50.D8 specifies `G-T3` (a PROPERTY test: the
// fold equals the product at EVERY split point, over random presence sequences, with associativity
// over every pair of split points) and `G-T4` (a planted template oscillating around the `top_k`
// rank boundary with constant presence). Both are QA's, and neither is written here. What is here
// is the set of witnesses this implementation needs to be shippable rather than asserted: each law
// on ONE named witness, each rule at the seam where the producer and the composer meet, and the two
// negative arms that would otherwise be comments. A witness suite says the code does what it says;
// the property suite says it does so everywhere, and the second is not a reason to ship without the
// first.
//
// Determinism: no RNG, no threads, no wall clock. Every window uses a literal epoch-offset
// time_point. Single-threaded by construction.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using meta::PresenceChurn;
using meta::PresenceSymbol;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT1{std::chrono::seconds{1700000060}};
constexpr Clock::time_point kT2{std::chrono::seconds{1700000120}};

[[nodiscard]] std::string_view render(PresenceSymbol symbol)
{
    switch (symbol)
    {
    case PresenceSymbol::EmptyRange:
        return "empty-range";
    case PresenceSymbol::Unretained:
        return "unretained";
    case PresenceSymbol::Absent:
        return "absent";
    case PresenceSymbol::Present:
        return "present";
    }
    return "?";
}

[[nodiscard]] std::string render(const PresenceChurn& churn)
{
    return "(first=" + std::string{render(churn.first)} +
           " transitions=" + std::to_string(churn.transitions) +
           " indeterminate=" + std::to_string(churn.indeterminate) +
           " last=" + std::string{render(churn.last)} +
           " span_windows=" + std::to_string(churn.span_windows) + ")";
}

// One base window, from its presence symbol — the shape every element in this file is built from.
[[nodiscard]] constexpr PresenceChurn window(PresenceSymbol symbol)
{
    return {
        .span_windows = 1, .transitions = 0, .indeterminate = 0, .first = symbol, .last = symbol};
}

// Fold a whole presence sequence left to right — the reference the product is checked against.
// `std::array` with CTAD rather than an initializer_list: the sequence length is a compile-time
// property of each witness, and nothing here needs a heap.
template <std::size_t kLength>
[[nodiscard]] PresenceChurn fold(const std::array<PresenceSymbol, kLength>& sequence)
{
    PresenceChurn accumulated{};
    for (const PresenceSymbol symbol : sequence)
        accumulated = meta::compose_presence_churn(accumulated, window(symbol));
    return accumulated;
}

// The composed retained set's element for one template, by id, over top_k then reservoir.
[[nodiscard]] std::optional<PresenceChurn> churn_of(const meta::MetaLogDocument& doc,
                                                    const insight::TemplateId& tid)
{
    for (const auto& entry : doc.stats.top_k)
        if (entry.template_id == tid)
            return entry.presence_churn;
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_id == tid)
            return entry.presence_churn;
    return std::nullopt;
}

[[nodiscard]] std::string render_top_k(const meta::MetaLogDocument& doc)
{
    std::string out{"top_k[" + std::to_string(doc.stats.top_k.size()) +
                    "] tail_unique=" + std::to_string(doc.stats.tail_unique) +
                    " tail_count=" + std::to_string(doc.stats.tail_count) + ":"};
    for (const auto& entry : doc.stats.top_k)
        out += "\n      " + insight::render(entry.template_id) +
               " count=" + std::to_string(entry.count) + " churn=" + render(entry.presence_churn);
    return out;
}

// ── The algebra, on elements alone ────────────────────────────────────────────

// The definition itself: transitions count the boundaries where the presence bit CHANGED, and
// nothing else. Present-Absent-Present-Present over four windows has two of them.
TEST(PresenceChurnMonoid, TransitionsCountOnlyTheBoundariesWherePresenceChanged)
{
    const PresenceChurn folded{fold(std::array{PresenceSymbol::Present, PresenceSymbol::Absent,
                                               PresenceSymbol::Present, PresenceSymbol::Present})};
    EXPECT_EQ(folded.span_windows, 4U) << render(folded);
    EXPECT_EQ(folded.transitions, 2U)
        << "presence sequence 1,0,1,1 changes at boundaries 1->2 and 2->3 and nowhere else. Got "
        << render(folded);
    EXPECT_EQ(folded.indeterminate, 0U)
        << "no window truncated: nothing was unreadable. Got " << render(folded);
    EXPECT_EQ(folded.first, PresenceSymbol::Present) << render(folded);
    EXPECT_EQ(folded.last, PresenceSymbol::Present) << render(folded);
}

// The identity law, from BOTH sides, because the product's two short circuits are separate code.
TEST(PresenceChurnMonoid, TheEmptyRangeIsTheIdentityFromBothSides)
{
    const PresenceChurn subject{fold(std::array{PresenceSymbol::Present, PresenceSymbol::Absent})};
    const PresenceChurn identity{};

    ASSERT_EQ(identity.span_windows, 0U) << "a default-constructed element IS the identity";
    EXPECT_EQ(meta::compose_presence_churn(identity, subject), subject)
        << "e.B must equal B. e=" << render(identity) << " B=" << render(subject)
        << " got=" << render(meta::compose_presence_churn(identity, subject));
    EXPECT_EQ(meta::compose_presence_churn(subject, identity), subject)
        << "B.e must equal B. B=" << render(subject)
        << " got=" << render(meta::compose_presence_churn(subject, identity));
}

// THE NEGATIVE ARM THE TWO-SYMBOL RULE EXISTS FOR. `optional<bool>` is the obvious representation
// and it is the wrong one: it has ONE absent state, so the empty range and the not-retained-here
// range become the same value. This arm shows they cannot be, by measuring what the collapse would
// cost — an empty range contributes NO boundary term, an unretained one contributes an
// indeterminate. If a future edit merged the two symbols, this comparison goes from unequal to
// equal and the arm reds.
TEST(PresenceChurnMonoid, CollapsingTheTwoAbsentSymbolsWouldBreakTheIdentityLaw)
{
    const PresenceChurn subject{fold(std::array{PresenceSymbol::Present, PresenceSymbol::Present})};
    const PresenceChurn empty_range{};
    const PresenceChurn unretained_range{
        meta::presence_churn_of_unretained_range(1U, /*retention_exhaustive=*/false)};

    ASSERT_EQ(unretained_range.first, PresenceSymbol::Unretained) << render(unretained_range);

    const PresenceChurn via_empty{meta::compose_presence_churn(empty_range, subject)};
    const PresenceChurn via_unretained{meta::compose_presence_churn(unretained_range, subject)};

    EXPECT_EQ(via_empty, subject) << "the empty range is the identity: " << render(via_empty);
    EXPECT_NE(via_unretained, subject)
        << "an UNRETAINED range is not the identity — it adds a window and declares its boundary "
           "unreadable. If this compares EQUAL, the two absent symbols have been collapsed and the "
           "identity law no longer distinguishes them. empty->"
        << render(via_empty) << " unretained->" << render(via_unretained);
    EXPECT_EQ(via_unretained.indeterminate, 1U)
        << "the unretained boundary must be DECLARED, not silently counted as no-transition. Got "
        << render(via_unretained);
    EXPECT_EQ(via_unretained.transitions, subject.transitions)
        << "and it must never be counted as a transition. Got " << render(via_unretained);
}

// THE ORDER ARM. The boundary term reads (last(A), first(B)), so the product is orientation-
// sensitive. A fold applied in the wrong order is deterministic and WRONG — which is why the
// witness matters: no determinism gate can see it.
TEST(PresenceChurnMonoid, TheProductIsNotCommutativeOnAWitnessPair)
{
    const PresenceChurn present_then_absent{
        fold(std::array{PresenceSymbol::Present, PresenceSymbol::Absent})};
    const PresenceChurn absent_only{window(PresenceSymbol::Absent)};

    const PresenceChurn forward{meta::compose_presence_churn(present_then_absent, absent_only)};
    const PresenceChurn backward{meta::compose_presence_churn(absent_only, present_then_absent)};

    EXPECT_NE(forward, backward)
        << "the window-order MUST has no teeth if the product is symmetric. forward="
        << render(forward) << " backward=" << render(backward);
    EXPECT_EQ(forward.transitions, 1U) << "1,0,0 changes once. Got " << render(forward);
    EXPECT_EQ(backward.transitions, 2U) << "0,1,0 changes twice. Got " << render(backward);
}

// Associativity on one witness triple — the property that lets a single element per block fold at
// every ladder level with no re-scan of the base windows. The exhaustive sweep is `G-T3`.
TEST(PresenceChurnMonoid, AssociativityHoldsOnAWitnessTriple)
{
    const PresenceChurn a{fold(std::array{PresenceSymbol::Present, PresenceSymbol::Absent})};
    const PresenceChurn b{fold(std::array{PresenceSymbol::Present, PresenceSymbol::Present})};
    const PresenceChurn c{fold(std::array{PresenceSymbol::Absent, PresenceSymbol::Present})};

    const PresenceChurn left{meta::compose_presence_churn(meta::compose_presence_churn(a, b), c)};
    const PresenceChurn right{meta::compose_presence_churn(a, meta::compose_presence_churn(b, c))};

    EXPECT_EQ(left, right) << "(A.B).C=" << render(left) << " A.(B.C)=" << render(right);
    EXPECT_EQ(left, fold(std::array{PresenceSymbol::Present, PresenceSymbol::Absent,
                                    PresenceSymbol::Present, PresenceSymbol::Present,
                                    PresenceSymbol::Absent, PresenceSymbol::Present}))
        << "and both must equal the flat left fold of the same six windows. Got " << render(left);
}

// ── The producer and the composer ─────────────────────────────────────────────

[[nodiscard]] meta::MetaLogConfig churn_config(std::size_t top_k_size)
{
    return meta::MetaLogConfig{
        .top_k_size = top_k_size,
        .reservoir_size = 0,
        .top_ngrams_size = 0, // no behavior block: every arm here is about `stats`
        .emit_stability = false,
        .max_param_histograms = 0,
    };
}

// The base case: one observed window stamps the one-window element on every retained row and opens
// the roll-up at span 1. `transitions <= span_windows - 1` makes that roll-up all-zero by
// construction, which is exactly why the wire omits it (asserted further down).
TEST(PresenceChurnProducer, AnObservedWindowStampsTheOneWindowElementOnEveryRetainedRow)
{
    meta::MetaLogEngine engine{churn_config(8)};
    engine.open_window(kT0);
    for (int rep = 0; rep < 5; ++rep)
    {
        engine.ingest_event(make_event("alpha steady event"));
        engine.ingest_event(make_event("beta steady event"));
    }
    const auto doc{engine.close_window(kT1)};

    ASSERT_EQ(doc.stats.top_k.size(), 2U) << render_top_k(doc);
    for (const auto& entry : doc.stats.top_k)
    {
        EXPECT_EQ(entry.presence_churn, meta::presence_churn_of_retained_window())
            << insight::render(entry.template_id) << " -> " << render(entry.presence_churn);
    }
    ASSERT_TRUE(doc.presence_churn.has_value()) << "an observed window opens the roll-up";
    EXPECT_EQ(doc.presence_churn->span_windows, 1U);
    EXPECT_EQ(doc.presence_churn->templates_with_churn, 0U)
        << "a one-window range has no boundary, so it cannot have a template with churn";
    EXPECT_EQ(doc.presence_churn->total_transitions, 0U);
    EXPECT_EQ(doc.presence_churn->total_indeterminate, 0U);
    EXPECT_TRUE(meta::retention_is_exhaustive(doc.stats))
        << "precondition: this window retained everything it observed, so `Absent` is reachable "
           "for a template that leaves it. "
        << render_top_k(doc);
}

// SPEC §12.2's ZERO — `lines_observed = 0` and empty stats — is DN-50.D4's empty block. Mapping the
// two onto one state is what makes the monoid's identity law and the standard's identity MUST the
// same law, and this arm is where that identification is checked rather than asserted in a comment.
TEST(PresenceChurnProducer, AnEventFreeWindowIsTheMonoidIdentityAndComposeIdentityHolds)
{
    const auto cfg{churn_config(8)};
    meta::MetaLogEngine engine_a{cfg};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 5; ++rep)
        engine_a.ingest_event(make_event("alpha steady event"));
    const auto doc_a{engine_a.close_window(kT1)};

    meta::MetaLogEngine engine_zero{cfg};
    engine_zero.open_window(kT1);
    const auto zero{engine_zero.close_window(kT2)};

    ASSERT_EQ(zero.window.lines_observed, 0U);
    EXPECT_FALSE(zero.presence_churn.has_value())
        << "a ZERO document must carry NO churn observation — it is the identity, not a window in "
           "which everything was absent";

    const auto composed{meta::compose(doc_a, zero)};
    ASSERT_TRUE(composed.presence_churn.has_value());
    EXPECT_EQ(composed.presence_churn->span_windows, 1U)
        << "compose(A, ZERO) must leave A's span untouched; got "
        << composed.presence_churn->span_windows;
    ASSERT_EQ(composed.stats.top_k.size(), doc_a.stats.top_k.size()) << render_top_k(composed);
    EXPECT_EQ(composed.stats.top_k.front().presence_churn, doc_a.stats.top_k.front().presence_churn)
        << "compose(A, ZERO) must reproduce A's element exactly. A="
        << render(doc_a.stats.top_k.front().presence_churn)
        << " composed=" << render(composed.stats.top_k.front().presence_churn);
}

// THE WINDOW-ORDER MUST AT THE SEAM. `compose()` derives the orientation from the two documents'
// own window envelopes, so an argument order that disagrees with time order cannot change the
// result. The witness is asymmetric on purpose: a template present in the EARLIER window and absent
// from the later one folds to first=Present/last=Absent, and a backwards fold would produce the
// mirror — the same transition COUNT with the projections swapped, which is precisely what makes a
// consumer's re-fold of two adjacent documents wrong.
TEST(PresenceChurnCompose, TheFoldFollowsTheWindowEnvelopeAndNotTheArgumentOrder)
{
    const auto cfg{churn_config(8)};
    meta::MetaLogEngine engine_a{cfg};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 5; ++rep)
    {
        engine_a.ingest_event(make_event("alpha steady event"));
        engine_a.ingest_event(make_event("beta vanishing event"));
    }
    const auto earlier{engine_a.close_window(kT1)};

    meta::MetaLogEngine engine_b{cfg};
    engine_b.open_window(kT1);
    for (int rep = 0; rep < 5; ++rep)
        engine_b.ingest_event(make_event("alpha steady event"));
    const auto later{engine_b.close_window(kT2)};

    ASSERT_TRUE(meta::retention_is_exhaustive(earlier.stats)) << render_top_k(earlier);
    ASSERT_TRUE(meta::retention_is_exhaustive(later.stats))
        << "the later window must retain everything it observed, or `beta`'s absence is unknowable "
           "and this arm measures the wrong rule. "
        << render_top_k(later);
    const insight::TemplateId beta{earlier.stats.top_k.at(0).template_id ==
                                           later.stats.top_k.at(0).template_id
                                       ? earlier.stats.top_k.at(1).template_id
                                       : earlier.stats.top_k.at(0).template_id};

    const auto forward{meta::compose(earlier, later)};
    const auto backward{meta::compose(later, earlier)};

    const auto forward_beta{churn_of(forward, beta)};
    const auto backward_beta{churn_of(backward, beta)};
    ASSERT_TRUE(forward_beta.has_value()) << render_top_k(forward);
    ASSERT_TRUE(backward_beta.has_value()) << render_top_k(backward);

    EXPECT_EQ(forward_beta->span_windows, 2U) << render(*forward_beta);
    EXPECT_EQ(forward_beta->transitions, 1U)
        << "`beta` is present in the earlier window and absent from the later one. Got "
        << render(*forward_beta);
    EXPECT_EQ(forward_beta->indeterminate, 0U)
        << "both windows retained everything they observed: no boundary was unreadable. Got "
        << render(*forward_beta);
    EXPECT_EQ(forward_beta->first, PresenceSymbol::Present)
        << "the projections carry the ORIENTATION, and this one must read present->absent. Got "
        << render(*forward_beta);
    EXPECT_EQ(forward_beta->last, PresenceSymbol::Absent) << render(*forward_beta);
    EXPECT_EQ(*backward_beta, *forward_beta)
        << "compose() must fold in WINDOW order whatever order its arguments arrive in. "
           "compose(earlier, later)="
        << render(*forward_beta) << " compose(later, earlier)=" << render(*backward_beta);
}

// THE INVENTED-CHURN RULE, at the seam. `top_k` truncates by COUNT, so absence from the retained
// set of a TRUNCATED window is not absence from the window. This fixture puts both readings in one
// composition: `alpha` leaves a window that truncated (unknowable -> indeterminate) while `gamma`
// enters from a window that retained everything (knowable -> a real transition). A composer that
// named retained-set membership AS presence would report a transition for `alpha` too, and that is
// the defect the fourth symbol exists to prevent.
TEST(PresenceChurnCompose, AbsenceFromATruncatedWindowIsIndeterminateNotATransition)
{
    meta::MetaLogEngine engine_a{churn_config(2)};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 10; ++rep)
        engine_a.ingest_event(make_event("alpha steady event"));
    for (int rep = 0; rep < 5; ++rep)
        engine_a.ingest_event(make_event("beta steady event"));
    const auto earlier{engine_a.close_window(kT1)};

    meta::MetaLogEngine engine_b{churn_config(2)};
    engine_b.open_window(kT1);
    for (int rep = 0; rep < 20; ++rep)
        engine_b.ingest_event(make_event("gamma steady event"));
    for (int rep = 0; rep < 3; ++rep)
        engine_b.ingest_event(make_event("delta steady event"));
    engine_b.ingest_event(make_event("epsilon steady event"));
    const auto later{engine_b.close_window(kT2)};

    ASSERT_TRUE(meta::retention_is_exhaustive(earlier.stats))
        << "the earlier window must retain everything, so `gamma`'s absence there is DEFINITE. "
        << render_top_k(earlier);
    ASSERT_FALSE(meta::retention_is_exhaustive(later.stats))
        << "the later window must TRUNCATE, or the indeterminate arm below is vacuous. "
        << render_top_k(later);

    const insight::TemplateId alpha{earlier.stats.top_k.at(0).template_id};
    const insight::TemplateId gamma{later.stats.top_k.at(0).template_id};

    const auto composed{meta::compose(earlier, later)};
    const auto alpha_churn{churn_of(composed, alpha)};
    const auto gamma_churn{churn_of(composed, gamma)};
    ASSERT_TRUE(alpha_churn.has_value()) << render_top_k(composed);
    ASSERT_TRUE(gamma_churn.has_value()) << render_top_k(composed);

    EXPECT_EQ(alpha_churn->transitions, 0U)
        << "`alpha` is not in the later window's RETAINED set, but that window truncated — it may "
           "have been in the tail. Reporting a transition here is invented churn. Got "
        << render(*alpha_churn);
    EXPECT_EQ(alpha_churn->indeterminate, 1U)
        << "and the boundary must be DECLARED unreadable rather than silently dropped. Got "
        << render(*alpha_churn);
    EXPECT_EQ(alpha_churn->last, PresenceSymbol::Unretained) << render(*alpha_churn);

    EXPECT_EQ(gamma_churn->transitions, 1U)
        << "`gamma` is absent from a window that retained EVERYTHING it observed, so its absence "
           "is a definite 0 and its arrival is a real transition. Got "
        << render(*gamma_churn);
    EXPECT_EQ(gamma_churn->indeterminate, 0U) << render(*gamma_churn);
    EXPECT_EQ(gamma_churn->first, PresenceSymbol::Absent) << render(*gamma_churn);

    ASSERT_TRUE(composed.presence_churn.has_value());
    EXPECT_EQ(composed.presence_churn->span_windows, 2U);
    EXPECT_EQ(composed.presence_churn->templates_with_churn, 1U)
        << "exactly one retained template moved: `gamma`";
    EXPECT_EQ(composed.presence_churn->total_transitions, 1U);
    EXPECT_EQ(composed.presence_churn->total_indeterminate, 1U);
}

// THE EMIT GATE. A one-window range cannot carry a transition, so its block would be members whose
// values another member already fixes; the composed document is the first range that can say
// anything, and it must.
TEST(PresenceChurnWire, TheSpanOneBlockIsOmittedAndTheComposedOneIsEmitted)
{
    const auto cfg{churn_config(8)};
    meta::MetaLogEngine engine_a{cfg};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 5; ++rep)
    {
        engine_a.ingest_event(make_event("alpha steady event"));
        engine_a.ingest_event(make_event("beta vanishing event"));
    }
    const auto earlier{engine_a.close_window(kT1)};

    meta::MetaLogEngine engine_b{cfg};
    engine_b.open_window(kT1);
    for (int rep = 0; rep < 5; ++rep)
        engine_b.ingest_event(make_event("alpha steady event"));
    const auto later{engine_b.close_window(kT2)};

    const std::string base_json{meta::to_json(earlier, engine_a.registry())};
    EXPECT_EQ(base_json.find("fr.coderoast.presence_churn"), std::string::npos)
        << "a single-window document must carry no churn block at all";

    const std::string composed_json{
        meta::to_json(meta::compose(earlier, later), engine_a.registry())};
    EXPECT_NE(composed_json.find("\"fr.coderoast.presence_churn\""), std::string::npos)
        << "the composed document must carry the per-row block";
    EXPECT_NE(composed_json.find("\"fr.coderoast.presence_churn_summary\""), std::string::npos)
        << "and the document-root roll-up";
    EXPECT_NE(composed_json.find("top_k_union_reservoir"), std::string::npos)
        << "the roll-up must DECLARE its presence predicate, or `indeterminate` is uncomparable "
           "across producers";
}

} // namespace
