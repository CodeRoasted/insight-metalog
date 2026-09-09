// refs: DN-50.D11, DN-50.D13, DN-50.D4
// invariant: the measurand: what compose() may claim about a pair of documents whose window
// envelopes are NOT disjoint -- the hosted shard fold, N documents of ONE event-time interval.
// invariant: every shard of a composed window closes at the SAME boundary and opens at its OWN
// first event, so the shard documents OVERLAP and their start_iso values differ.
// invariant: the time-ordered churn product is defined only over DISJOINT envelopes, and a
// non-disjoint pair takes the SPATIAL JOIN instead.
// invariant: the join takes the MAX of the two spans and the SUMS of their own transitions and
// indeterminates, and applies no boundary term at all.
// invariant: the spatial join is symmetric, so no corner remains where churn reads argument order.
// invariant: window.duration_seconds is the MERGED envelope's span in every geometry -- disjoint,
// adjacent, gapped, overlapping, nested, identical -- and never a sum of the inputs' durations.
// note: no RNG, no threads, no wall clock; every window uses a literal epoch-offset time_point.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using meta::MetaLogDocument;
using meta::PresenceChurn;
using meta::PresenceSymbol;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT20{std::chrono::seconds{1700000020}};
constexpr Clock::time_point kT30{std::chrono::seconds{1700000030}};
constexpr Clock::time_point kT40{std::chrono::seconds{1700000040}};
constexpr Clock::time_point kT60{std::chrono::seconds{1700000060}};
constexpr Clock::time_point kT90{std::chrono::seconds{1700000090}};
constexpr Clock::time_point kT120{std::chrono::seconds{1700000120}};

// invariant: alpha is ingested five times per shard and beta three, so the two templates are
// separable by count and top_k's count-descending order is not the thing under test.
constexpr std::uint64_t kAlphaPerShard{5};
constexpr std::uint64_t kBetaPerShard{3};

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

[[nodiscard]] std::string render_envelope(const MetaLogDocument& doc)
{
    return "[" + doc.window.start_iso + " .. " + doc.window.end_iso +
           "] duration_seconds=" + std::to_string(doc.window.duration_seconds);
}

[[nodiscard]] std::string render_top_k(const MetaLogDocument& doc)
{
    std::string out{"top_k[" + std::to_string(doc.stats.top_k.size()) +
                    "] tail_count=" + std::to_string(doc.stats.tail_count) + " " +
                    render_envelope(doc) + ":"};
    for (const auto& entry : doc.stats.top_k)
        out += "\n      " + insight::render(entry.template_id) +
               " count=" + std::to_string(entry.count) + " churn=" + render(entry.presence_churn);
    return out;
}

// note: the composed retained set's element for one template, by id, over top_k then reservoir.
[[nodiscard]] std::optional<PresenceChurn> churn_of(const MetaLogDocument& doc,
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

// invariant: reservoir and tail are off and top_k is wide, so every observed template is retained
// and an absence is DEFINITE rather than unknowable -- the precondition the churn rule reads.
[[nodiscard]] meta::MetaLogConfig shard_config()
{
    return meta::MetaLogConfig{
        .top_k_size = 8,
        .reservoir_size = 0,
        .top_ngrams_size = 0,
        .emit_stability = false,
        .max_param_histograms = 0,
    };
}

// post: one shard's document for a composed interval -- opened at that shard's OWN first event,
// closed at the boundary every shard of the interval shares.
// pre: carries_beta says whether the shard observed the template whose presence is under test.
[[nodiscard]] MetaLogDocument shard_document(Clock::time_point opened_at,
                                             Clock::time_point closed_at, bool carries_beta)
{
    meta::MetaLogEngine engine{shard_config()};
    engine.open_window(opened_at);
    for (std::uint64_t rep{0}; rep < kAlphaPerShard; ++rep)
        engine.ingest_event(make_event("alpha steady event"));
    if (carries_beta)
        for (std::uint64_t rep{0}; rep < kBetaPerShard; ++rep)
            engine.ingest_event(make_event("beta shard local event"));
    return engine.close_window(closed_at);
}

// post: the two template ids of a shard document that observed both, separated by count.
struct TemplatePair
{
    insight::TemplateId alpha;
    insight::TemplateId beta;
};

[[nodiscard]] TemplatePair templates_of(const MetaLogDocument& doc)
{
    EXPECT_EQ(doc.stats.top_k.size(), 2U)
        << "the fixture observes exactly two templates and retains both. " << render_top_k(doc);
    EXPECT_EQ(doc.stats.top_k.at(0).count, kAlphaPerShard)
        << "top_k is count-descending, so slot 0 is alpha. " << render_top_k(doc);
    EXPECT_EQ(doc.stats.top_k.at(1).count, kBetaPerShard)
        << "top_k is count-descending, so slot 1 is beta. " << render_top_k(doc);
    return {doc.stats.top_k.at(0).template_id, doc.stats.top_k.at(1).template_id};
}

// assert: the fixture really built the geometry the rule is about -- overlapping envelopes with
// DISTINCT starts and a SHARED end. Without this the arms below measure the disjoint case.
void require_shard_geometry(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    ASSERT_FALSE(lhs.window.start_iso.empty()) << render_envelope(lhs);
    ASSERT_FALSE(rhs.window.start_iso.empty()) << render_envelope(rhs);
    ASSERT_NE(lhs.window.start_iso, rhs.window.start_iso)
        << "each shard opens at its OWN first event, so the starts must differ or this fixture is "
           "not the shard fold. lhs="
        << render_envelope(lhs) << " rhs=" << render_envelope(rhs);
    ASSERT_EQ(lhs.window.end_iso, rhs.window.end_iso)
        << "every shard of one composed interval is closed at the SAME boundary. lhs="
        << render_envelope(lhs) << " rhs=" << render_envelope(rhs);
    ASSERT_LT(rhs.window.start_iso, lhs.window.end_iso)
        << "the envelopes must OVERLAP -- disjointness is `earlier.end <= later.start`, and if it "
           "held here the temporal product would be the correct rule. lhs="
        << render_envelope(lhs) << " rhs=" << render_envelope(rhs);
}

// refs: DN-50.D11
// invariant: a template observed by one shard and not by another of the SAME interval differs
// SPATIALLY, and a spatial difference is not a transition.
// invariant: the join reports the fleet's presence -- Present if any shard saw it -- because that
// is the only reading `present in this window` admits across sources.
TEST(ComposeShardFold, ANonDisjointPairSpansOneWindowAndInventsNoTransition)
{
    const MetaLogDocument a{shard_document(kT0, kT60, /*carries_beta=*/true)};
    const MetaLogDocument b{shard_document(kT30, kT60, /*carries_beta=*/false)};

    ASSERT_NO_FATAL_FAILURE(require_shard_geometry(a, b));
    ASSERT_TRUE(meta::retention_is_exhaustive(a.stats))
        << "beta's absence from the other shard must be DEFINITE, or this arm measures the "
           "indeterminate rule instead. "
        << render_top_k(a);
    ASSERT_TRUE(meta::retention_is_exhaustive(b.stats)) << render_top_k(b);

    const TemplatePair ids{templates_of(a)};
    const MetaLogDocument composed{meta::compose(a, b)};

    const auto beta_churn{churn_of(composed, ids.beta)};
    ASSERT_TRUE(beta_churn.has_value()) << render_top_k(composed);
    EXPECT_EQ(beta_churn->span_windows, 1U)
        << "G-C1: two shards of ONE event-time interval are ONE base window, not two. Summing the "
           "spans makes span_windows the SHARD COUNT and over-reports every hosted document by N. "
           "Got "
        << render(*beta_churn) << " for beta. " << render_top_k(composed);
    EXPECT_EQ(beta_churn->transitions, 0U)
        << "G-C1: beta was observed by one shard and not the other WITHIN one interval. That is a "
           "spatial difference between sources, and reading it as a boundary term publishes churn "
           "that never happened. Got "
        << render(*beta_churn) << " for beta. " << render_top_k(composed);
    EXPECT_EQ(beta_churn->indeterminate, 0U)
        << "both shards retained everything they observed: no boundary was unreadable. Got "
        << render(*beta_churn);
    EXPECT_EQ(beta_churn->first, PresenceSymbol::Present)
        << "G-C1: presence over the FLEET window is Present when ANY shard observed it. Got "
        << render(*beta_churn);
    EXPECT_EQ(beta_churn->last, PresenceSymbol::Present)
        << "G-C1: the join has no orientation, so first and last are the same fleet symbol. Got "
        << render(*beta_churn);

    const auto alpha_churn{churn_of(composed, ids.alpha)};
    ASSERT_TRUE(alpha_churn.has_value()) << render_top_k(composed);
    EXPECT_EQ(alpha_churn->span_windows, 1U)
        << "G-C1: the span is a property of the INTERVAL, so a template every shard saw carries "
           "the same span as one only a single shard saw. Got "
        << render(*alpha_churn) << " for alpha.";
    EXPECT_EQ(alpha_churn->transitions, 0U) << render(*alpha_churn);

    ASSERT_TRUE(composed.presence_churn.has_value())
        << "the document-root roll-up must be re-derived from the rows. " << render_top_k(composed);
    EXPECT_EQ(composed.presence_churn->span_windows, 1U)
        << "G-C1: the roll-up carries the same span as its rows. Got "
        << composed.presence_churn->span_windows;
    EXPECT_EQ(composed.presence_churn->total_transitions, 0U)
        << "G-C1: no template moved, because nothing about this interval is a boundary. Got "
        << composed.presence_churn->total_transitions;
    EXPECT_EQ(composed.presence_churn->templates_with_churn, 0U)
        << composed.presence_churn->templates_with_churn;
}

// refs: DN-50.D11
// invariant: the join is SYMMETRIC, so the shard fold's result cannot depend on the order the
// pipeline happened to collect its shards in.
TEST(ComposeShardFold, TheJoinOverANonDisjointPairIsSymmetric)
{
    const MetaLogDocument a{shard_document(kT0, kT60, /*carries_beta=*/true)};
    const MetaLogDocument b{shard_document(kT30, kT60, /*carries_beta=*/false)};
    ASSERT_NO_FATAL_FAILURE(require_shard_geometry(a, b));

    const TemplatePair ids{templates_of(a)};
    const auto forward{churn_of(meta::compose(a, b), ids.beta)};
    const auto backward{churn_of(meta::compose(b, a), ids.beta)};
    ASSERT_TRUE(forward.has_value());
    ASSERT_TRUE(backward.has_value());

    EXPECT_EQ(*forward, *backward)
        << "compose() over a NON-disjoint pair must not read argument order. compose(a, b)="
        << render(*forward) << " compose(b, a)=" << render(*backward);
    EXPECT_EQ(meta::compose(a, b).window.duration_seconds,
              meta::compose(b, a).window.duration_seconds)
        << "the merged envelope is symmetric, so its span is too. compose(a, b)="
        << render_envelope(meta::compose(a, b))
        << " compose(b, a)=" << render_envelope(meta::compose(b, a));
}

// refs: DN-50.D11, DN-50.D4
// invariant: the temporal product survives the correction unchanged over DISJOINT envelopes -- a
// real boundary between two adjacent base windows still counts a transition.
// note: this is the negative control for the join: it must be green both before and after it.
TEST(ComposeShardFold, ADisjointAdjacentPairStillTakesTheTemporalProduct)
{
    const MetaLogDocument first{shard_document(kT0, kT60, /*carries_beta=*/true)};
    const MetaLogDocument second{shard_document(kT60, kT120, /*carries_beta=*/false)};

    ASSERT_LE(first.window.end_iso, second.window.start_iso)
        << "the control's envelopes must be DISJOINT, or it controls nothing. first="
        << render_envelope(first) << " second=" << render_envelope(second);
    ASSERT_LT(first.window.start_iso, second.window.start_iso)
        << "and ordered. first=" << render_envelope(first)
        << " second=" << render_envelope(second);
    ASSERT_TRUE(meta::retention_is_exhaustive(second.stats)) << render_top_k(second);

    const TemplatePair ids{templates_of(first)};
    const MetaLogDocument composed{meta::compose(first, second)};

    const auto beta_churn{churn_of(composed, ids.beta)};
    ASSERT_TRUE(beta_churn.has_value()) << render_top_k(composed);
    EXPECT_EQ(beta_churn->span_windows, 2U)
        << "G-C2 control: two DISJOINT windows are two base windows. Got " << render(*beta_churn);
    EXPECT_EQ(beta_churn->transitions, 1U)
        << "G-C2 control: beta left between two adjacent windows -- a REAL boundary. Narrowing the "
           "temporal product to disjoint pairs must not cost this. Got "
        << render(*beta_churn);
    EXPECT_EQ(beta_churn->first, PresenceSymbol::Present) << render(*beta_churn);
    EXPECT_EQ(beta_churn->last, PresenceSymbol::Absent)
        << "G-C2 control: the temporal product keeps its ORIENTATION. Got " << render(*beta_churn);
    EXPECT_EQ(composed.window.duration_seconds, 120U)
        << "G-C2 control: the merged envelope spans both windows end to end. "
        << render_envelope(composed);
}

// refs: DN-50.D11
// invariant: two fleet windows built by the join are themselves disjoint, and the ladder then
// applies the temporal product over them -- one boundary, two base windows, whatever N was.
TEST(ComposeShardFold, TwoJoinedFleetWindowsComposeAsTwoBaseWindowsNotFour)
{
    const MetaLogDocument fleet_one{meta::compose(shard_document(kT0, kT60, true),
                                                 shard_document(kT30, kT60, false))};
    const MetaLogDocument fleet_two{meta::compose(shard_document(kT60, kT120, false),
                                                 shard_document(kT90, kT120, false))};

    ASSERT_LE(fleet_one.window.end_iso, fleet_two.window.start_iso)
        << "the two fleet windows must be disjoint. one=" << render_envelope(fleet_one)
        << " two=" << render_envelope(fleet_two);

    const TemplatePair ids{templates_of(shard_document(kT0, kT60, true))};
    const MetaLogDocument composed{meta::compose(fleet_one, fleet_two)};

    const auto beta_churn{churn_of(composed, ids.beta)};
    ASSERT_TRUE(beta_churn.has_value()) << render_top_k(composed);
    EXPECT_EQ(beta_churn->span_windows, 2U)
        << "G-C2: two intervals of two shards each are TWO base windows. A span of 4 is the shard "
           "count leaking into the ladder, where it multiplies at every level. Got "
        << render(*beta_churn) << " " << render_top_k(composed);
    EXPECT_EQ(beta_churn->transitions, 1U)
        << "G-C2: beta is present in the first fleet window and absent from the second -- exactly "
           "one real boundary, and no shard-to-shard term beside it. Got "
        << render(*beta_churn);
    EXPECT_EQ(beta_churn->first, PresenceSymbol::Present) << render(*beta_churn);
    EXPECT_EQ(beta_churn->last, PresenceSymbol::Absent) << render(*beta_churn);
}

// refs: DN-50.D11
// invariant: the zero-width corner -- two documents whose envelopes are IDENTICAL have no
// derivable time order, so they take the join and never argument order.
TEST(ComposeShardFold, IdenticalZeroWidthEnvelopesTakeTheJoinAndNotTheArgumentOrder)
{
    const MetaLogDocument a{shard_document(kT0, kT0, /*carries_beta=*/true)};
    const MetaLogDocument b{shard_document(kT0, kT0, /*carries_beta=*/false)};

    ASSERT_EQ(a.window.start_iso, b.window.start_iso) << render_envelope(a);
    ASSERT_EQ(a.window.start_iso, a.window.end_iso)
        << "the corner under test is a ZERO-WIDTH envelope. " << render_envelope(a);
    ASSERT_EQ(a.window.duration_seconds, 0U) << render_envelope(a);

    const TemplatePair ids{templates_of(a)};
    const auto forward{churn_of(meta::compose(a, b), ids.beta)};
    const auto backward{churn_of(meta::compose(b, a), ids.beta)};
    ASSERT_TRUE(forward.has_value());
    ASSERT_TRUE(backward.has_value());

    EXPECT_EQ(*forward, *backward)
        << "G-C3: equal envelopes carry no order, so the result must not change with the argument "
           "order. compose(a, b)="
        << render(*forward) << " compose(b, a)=" << render(*backward);
    EXPECT_EQ(forward->span_windows, 1U)
        << "G-C3: one instant is one base window. Got " << render(*forward);
    EXPECT_EQ(forward->transitions, 0U)
        << "G-C3: a zero-width envelope has no interior boundary to change across. Got "
        << render(*forward);
    EXPECT_EQ(forward->first, PresenceSymbol::Present)
        << "G-C3: the fleet saw beta. Got " << render(*forward);
    EXPECT_EQ(forward->last, PresenceSymbol::Present) << render(*forward);
}

// refs: DN-50.D13
// invariant: duration_seconds is `end - start` of the MERGED envelope, which the composed document
// already carries -- so it is re-derivable by any consumer holding only the wire document.
TEST(ComposeShardFold, TheComposedDurationIsTheMergedEnvelopeSpanOnTwoShards)
{
    const MetaLogDocument a{shard_document(kT0, kT60, /*carries_beta=*/true)};
    const MetaLogDocument b{shard_document(kT30, kT60, /*carries_beta=*/false)};
    ASSERT_NO_FATAL_FAILURE(require_shard_geometry(a, b));
    ASSERT_EQ(a.window.duration_seconds, 60U) << render_envelope(a);
    ASSERT_EQ(b.window.duration_seconds, 30U) << render_envelope(b);

    const MetaLogDocument composed{meta::compose(a, b)};
    EXPECT_EQ(composed.window.start_iso, a.window.start_iso)
        << "the merged envelope opens at the earliest shard start. " << render_envelope(composed);
    EXPECT_EQ(composed.window.end_iso, a.window.end_iso)
        << "and closes at the shared boundary. " << render_envelope(composed);
    EXPECT_EQ(composed.window.duration_seconds, 60U)
        << "G-C4: two shards observed ONE 60-second interval. Summing their spans (60 + 30) claims "
           "90 seconds of real time that did not elapse, and every rate a consumer forms as "
           "lines_observed / duration_seconds is wrong by that factor. "
        << render_envelope(composed);
}

// refs: DN-50.D13
// invariant: the sum grows with the SHARD COUNT while the interval does not, so the error is
// unbounded in N -- this arm fixes N at 3 to make that visible as one number.
TEST(ComposeShardFold, TheComposedDurationDoesNotGrowWithTheShardCount)
{
    const MetaLogDocument a{shard_document(kT0, kT60, /*carries_beta=*/true)};
    const MetaLogDocument b{shard_document(kT20, kT60, /*carries_beta=*/false)};
    const MetaLogDocument c{shard_document(kT40, kT60, /*carries_beta=*/false)};
    ASSERT_EQ(a.window.duration_seconds, 60U) << render_envelope(a);
    ASSERT_EQ(b.window.duration_seconds, 40U) << render_envelope(b);
    ASSERT_EQ(c.window.duration_seconds, 20U) << render_envelope(c);

    const MetaLogDocument composed{meta::compose(meta::compose(a, b), c)};
    EXPECT_EQ(composed.window.start_iso, a.window.start_iso) << render_envelope(composed);
    EXPECT_EQ(composed.window.end_iso, a.window.end_iso) << render_envelope(composed);
    EXPECT_EQ(composed.window.duration_seconds, 60U)
        << "G-C4: three shards observed the SAME 60-second interval. A pairwise sum reads 120 "
           "seconds here and would read more at four shards, so the hosted window-duration figure "
           "scales with a deployment parameter rather than with elapsed time. "
        << render_envelope(composed);

    const auto beta_churn{churn_of(composed, templates_of(a).beta)};
    ASSERT_TRUE(beta_churn.has_value()) << render_top_k(composed);
    EXPECT_EQ(beta_churn->span_windows, 1U)
        << "G-C4: and the same fold reads span_windows as the shard count. Got "
        << render(*beta_churn);
    EXPECT_EQ(beta_churn->transitions, 0U)
        << "G-C4: one interval, one presence fact, no boundary. Got " << render(*beta_churn);
}

} // namespace
