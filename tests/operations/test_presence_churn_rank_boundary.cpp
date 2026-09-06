
// refs: DN-50.D4, DN-50.D8
// invariant: this is G-T4, the invented-churn arm: top_k truncates by COUNT, so a template whose
// rank oscillates around the k-th position flips retained-set membership with the world unchanged.
// invariant: reading membership AS presence would then publish a content claim whose value is a
// function of one of OUR retention parameters, and it would arrive inside the document.
// invariant: the outcome is PRE-REGISTERED by DN-50.D8 before the fixture was written and not
// adjusted to what the code produced: transitions == 0 and indeterminate > 0.
// note: zero alone is not the bar, because a statistic that never fires is zero everywhere.
// invariant: the vacuity antidote is in this file on purpose: one arm requires silence where
// presence is constant, the other noise where presence moves.
// note: either arm alone is satisfied by a broken implementation, a dead counter or a naive one.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using meta::PresenceChurn;
using meta::PresenceSymbol;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kEpoch{std::chrono::seconds{1700000000}};
constexpr std::chrono::seconds kWindowWidth{60};

[[nodiscard]] constexpr Clock::time_point window_edge(int index)
{
    return kEpoch + kWindowWidth * index;
}

// invariant: the retained set holds k=3 while FOUR templates are live, so it can never be
// exhaustive.
// invariant: that is what makes Unretained rather than Absent the symbol on the truncated side;
// Absent would make the boundary readable and the transition real.
constexpr std::size_t kTopK{3};
constexpr std::size_t kOscillatingWindows{4};

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
    return std::format("(span={} trans={} indet={} first={} last={})", churn.span_windows,
                       churn.transitions, churn.indeterminate, render(churn.first),
                       render(churn.last));
}

[[nodiscard]] std::string render_top_k(const meta::MetaLogDocument& doc)
{
    std::string out{std::format("top_k[{}] tail_unique={} tail_count={}:", doc.stats.top_k.size(),
                                doc.stats.tail_unique, doc.stats.tail_count)};
    for (const auto& entry : doc.stats.top_k)
        out += std::format("\n      {} count={} churn={}", insight::render(entry.template_id),
                           entry.count, render(entry.presence_churn));
    return out;
}

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

[[nodiscard]] bool is_retained(const meta::MetaLogDocument& doc, const insight::TemplateId& tid)
{
    return churn_of(doc, tid).has_value();
}

[[nodiscard]] meta::MetaLogConfig rank_boundary_config()
{
    return meta::MetaLogConfig{
        .top_k_size = kTopK,
        .reservoir_size = 0,
        // note: the horizon under test is top_k truncation alone.
        .top_ngrams_size = 0,
        .emit_stability = false,
        .max_param_histograms = 0,
    };
}

constexpr std::string_view kAnchorHigh{"anchor high volume event"};
constexpr std::string_view kAnchorMid{"anchor mid volume event"};
constexpr std::string_view kOscillator{"planted rank boundary event"};
constexpr std::string_view kRival{"rival for the third slot event"};

// invariant: the planted template is emitted in EVERY window, so its PRESENCE is constant by
// construction and only its RANK moves -- the do-operator this gate needs.
// note: a fixture that dropped it from alternate windows would test real churn and still pass.
[[nodiscard]] meta::MetaLogDocument planted_window(int index, bool oscillator_retained)
{
    meta::MetaLogEngine engine{rank_boundary_config()};
    engine.open_window(window_edge(index));
    const auto emit{[&engine](std::string_view tmpl, int repetitions)
                    {
                        for (int rep = 0; rep < repetitions; ++rep)
                            engine.ingest_event(make_event(tmpl));
                    }};
    emit(kAnchorHigh, 100);
    emit(kAnchorMid, 90);
    // invariant: counts are distinct on both sides of every comparison, so the count-desc / id-asc
    // order never reaches the id tiebreak.
    // note: so the arrangement is a property of the fixture rather than of a content hash.
    emit(kOscillator, oscillator_retained ? 85 : 35);
    emit(kRival, oscillator_retained ? 5 : 40);
    return engine.close_window(window_edge(index + 1));
}

// note: folded in window order exactly as the pyramid's ladder folds a block.
[[nodiscard]] meta::MetaLogDocument planted_range()
{
    std::optional<meta::MetaLogDocument> composed;
    for (std::size_t n = 0; n < kOscillatingWindows; ++n)
    {
        auto window{planted_window(static_cast<int>(n), /*oscillator_retained=*/n % 2 == 0)};
        composed = composed ? meta::compose(*composed, window) : std::move(window);
    }
    return std::move(*composed);
}

[[nodiscard]] insight::TemplateId template_id_of(std::string_view tmpl)
{
    meta::MetaLogEngine engine{rank_boundary_config()};
    engine.open_window(window_edge(0));
    engine.ingest_event(make_event(tmpl));
    const auto doc{engine.close_window(window_edge(1))};
    return doc.stats.top_k.front().template_id;
}

// pre: the trap exists only if the rank really oscillates and the retained set is really truncated;
// both are properties of the counts above, so a failure here is about the FIXTURE.
TEST(PresenceChurnRankBoundary, ThePlantedTemplateIsAlwaysPresentAndItsRankOscillates)
{
    const insight::TemplateId oscillator{template_id_of(kOscillator)};
    const insight::TemplateId rival{template_id_of(kRival)};

    for (std::size_t n = 0; n < kOscillatingWindows; ++n)
    {
        const bool retained{n % 2 == 0};
        const auto doc{planted_window(static_cast<int>(n), retained)};
        ASSERT_EQ(doc.stats.top_k.size(), kTopK) << "window " << n << ": " << render_top_k(doc);
        EXPECT_FALSE(meta::retention_is_exhaustive(doc.stats))
            << "window " << n
            << " must TRUNCATE, or the truncated side reads `Absent` and the boundary becomes "
               "readable — the trap dissolves. "
            << render_top_k(doc);
        EXPECT_EQ(is_retained(doc, oscillator), retained)
            << "window " << n << " rank arrangement did not take effect. " << render_top_k(doc);
        EXPECT_EQ(is_retained(doc, rival), !retained)
            << "window " << n
            << ": the rival must occupy the third slot exactly when the planted "
               "template does not. "
            << render_top_k(doc);
    }
}

// refs: DN-50.D8
// invariant: the gate proper, pre-registered: transitions == 0 and indeterminate > 0.
TEST(PresenceChurnRankBoundary, ConstantPresenceAcrossTheRankBoundaryInventsNoTransition)
{
    const insight::TemplateId oscillator{template_id_of(kOscillator)};
    const auto composed{planted_range()};

    const auto churn{churn_of(composed, oscillator)};
    ASSERT_TRUE(churn.has_value())
        << "the planted template must survive into the composed retained set, or this gate reads "
           "nothing. "
        << render_top_k(composed);

    EXPECT_EQ(churn->transitions, 0U)
        << "the planted template is present in EVERY window; its retained-set membership is what "
           "moved. Any transition here is invented churn — a content claim whose value is a "
           "function of `top_k_size`. Got "
        << render(*churn);
    EXPECT_GT(churn->indeterminate, 0U)
        << "and the boundaries must be DECLARED unreadable rather than silently counted as `no "
           "transition`, which would under-report real churn while looking like a measurement. Got "
        << render(*churn);
    EXPECT_EQ(churn->indeterminate, kOscillatingWindows - 1)
        << "every one of the range's boundaries crosses the retention cut, so every one of them is "
           "unreadable. Got "
        << render(*churn);
    EXPECT_EQ(churn->span_windows, kOscillatingWindows) << render(*churn);
}

// invariant: the roll-up must agree with the row, so a consumer reading only the summary reaches
// the same conclusion as one reading the row.
TEST(PresenceChurnRankBoundary, TheRollUpCountsTheResidueAndNotTheTemplate)
{
    const auto composed{planted_range()};
    ASSERT_TRUE(composed.presence_churn.has_value()) << render_top_k(composed);
    const auto& summary{*composed.presence_churn};

    EXPECT_EQ(summary.span_windows, kOscillatingWindows);
    EXPECT_EQ(summary.templates_with_churn, 0U)
        << "no template's PRESENCE moved in this range; only ranks did. A non-zero count here is "
           "the invented-churn defect reaching the document root.";
    EXPECT_EQ(summary.total_transitions, 0U);
    EXPECT_GT(summary.total_indeterminate, 0U)
        << "the document must declare how much the retention grain hid — `no churn` and `churn we "
           "could not see` are different findings and must stay distinguishable from the document "
           "alone.";
}

// invariant: the rejected implementation is computed here over the SAME windows and reports three
// transitions where the world did not move.
// note: without that number, a zero is equally consistent with a counter that never fires.
TEST(PresenceChurnRankBoundary, TheNaiveMembershipReadingWouldHaveInventedThreeTransitions)
{
    const insight::TemplateId oscillator{template_id_of(kOscillator)};

    std::vector<bool> membership;
    membership.reserve(kOscillatingWindows);
    for (std::size_t n = 0; n < kOscillatingWindows; ++n)
        membership.push_back(is_retained(
            planted_window(static_cast<int>(n), /*oscillator_retained=*/n % 2 == 0), oscillator));

    std::uint32_t naive_transitions{0};
    for (std::size_t i = 1; i < membership.size(); ++i)
        if (membership[i] != membership[i - 1])
            ++naive_transitions;

    EXPECT_EQ(naive_transitions, kOscillatingWindows - 1)
        << "the fixture must actually EXHIBIT the trap: reading membership as presence has to "
           "produce churn here, or the gate above is green for want of a defect rather than for "
           "want of a bug.";

    const auto churn{churn_of(planted_range(), oscillator)};
    ASSERT_TRUE(churn.has_value());
    EXPECT_LT(churn->transitions, naive_transitions)
        << "the shipped statistic must be STRICTLY quieter than the naive one on this input; that "
           "difference is the whole content of DN-50.D4's ruling. shipped="
        << render(*churn) << " naive_transitions=" << naive_transitions;
}

// invariant: the antidote moves a template's REAL presence inside an EXHAUSTIVE retained set, so
// every absence is a definite Absent and every boundary is readable.
// note: the same four-window shape as the planted range, with presence rather than rank moving.
TEST(PresenceChurnRankBoundary, ARealPresenceOscillationIsStillCounted)
{
    const meta::MetaLogConfig cfg{
        .top_k_size = 8,
        .reservoir_size = 0,
        .top_ngrams_size = 0,
        .emit_stability = false,
        .max_param_histograms = 0,
    };

    const auto window{[&cfg](int index, bool oscillator_present)
                      {
                          meta::MetaLogEngine engine{cfg};
                          engine.open_window(window_edge(index));
                          for (int rep = 0; rep < 20; ++rep)
                              engine.ingest_event(make_event(kAnchorHigh));
                          if (oscillator_present)
                              for (int rep = 0; rep < 5; ++rep)
                                  engine.ingest_event(make_event(kOscillator));
                          return engine.close_window(window_edge(index + 1));
                      }};

    std::optional<meta::MetaLogDocument> composed;
    for (std::size_t n = 0; n < kOscillatingWindows; ++n)
    {
        auto doc{window(static_cast<int>(n), /*oscillator_present=*/n % 2 == 0)};
        ASSERT_TRUE(meta::retention_is_exhaustive(doc.stats))
            << "window " << n
            << " must retain everything it observed, or the absences below are `Unretained` and "
               "this arm measures the same thing as the one above. "
            << render_top_k(doc);
        composed = composed ? meta::compose(*composed, doc) : std::move(doc);
    }

    const insight::TemplateId oscillator{template_id_of(kOscillator)};
    const auto churn{churn_of(*composed, oscillator)};
    ASSERT_TRUE(churn.has_value()) << render_top_k(*composed);
    EXPECT_EQ(churn->transitions, kOscillatingWindows - 1)
        << "presence genuinely alternated across four windows with an exhaustive retained set, so "
           "every boundary is readable and every one of them moved. Got "
        << render(*churn);
    EXPECT_EQ(churn->indeterminate, 0U)
        << "nothing was hidden by retention here, so the residue must be empty. Got "
        << render(*churn);

    ASSERT_TRUE(composed->presence_churn.has_value());
    EXPECT_EQ(composed->presence_churn->templates_with_churn, 1U)
        << "exactly one template moved, and the roll-up must say so.";
}

} // namespace
