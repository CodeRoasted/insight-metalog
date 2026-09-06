// invariant: every entry in a reservoir this package PRODUCES carries an engaged retention_axis,
// and the disengaged state is a contract rather than dormant plumbing.
// invariant: exactly two sites fill a reservoir entry -- close_window's candidate collection and
// compose's re-derivation -- and each admits only under a positive salience score.
// invariant: the verdict stops being current the day an inbound path mints entries no salience
// computation produced; the property then narrows to those two producers.
// refs: DN-64.D6, DN-64.D3
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

// invariant: the whole population is carried so a failure prints every offender, not the first --
// an axis contract is about EVERY entry.
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

// invariant: named render_census because canon exports insight::render(TemplateId), and a
// same-named local overload would be a resolution puzzle inside a diagnostic.
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

// invariant: every member is a string literal with static storage.
// invariant: CanonicalEvent::template_str is a view, so a template built from a temporary would
// leave the engine reading freed bytes.
struct TemplateSet
{
    std::string_view steady_a;
    std::string_view steady_b;
    std::string_view steady_c;
    std::string_view off_path;
    std::string_view late;
    std::string_view rare_error;
};

// invariant: the two vocabularies are disjoint, so the compose arm merges two DIFFERENT populations
// rather than a document with a copy of itself.
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

// post: one window carrying THREE distinct retention causes -- a rare Error for severity, a
// recurring off-path benign Info for structure, a late self-looping Info for novelty.
// invariant: the steady bed makes all three rare by frequency, so none rides top_k instead.
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
    for (int rep = 0; rep < 3; ++rep)
    {
        engine.ingest_event(make_event(templates.steady_a));
        engine.ingest_event(make_event(templates.steady_b));
        engine.ingest_event(make_event(templates.off_path, insight::LogLevel::Info));
        engine.ingest_event(make_event(templates.steady_c));
    }
    for (int rep = 0; rep < 5; ++rep)
        engine.ingest_event(make_event(templates.late, insight::LogLevel::Info));
    engine.ingest_event(make_event(templates.rare_error, insight::LogLevel::Error));
    return engine.close_window(std::chrono::system_clock::time_point{} + std::chrono::seconds{60});
}
} // namespace

// refs: DN-64.D6
TEST(RetentionAxisCensus, EveryEntryClosedByCloseWindowCarriesAnEngagedAxis)
{
    const auto doc{close_window_with_three_causes(/*top_k=*/3, /*reservoir_size=*/8, kFirstWindow)};
    const Census census{census_of(doc)};

    // assert: the population is asserted non-empty first, because every claim below is vacuous on
    // an empty reservoir.
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

    // assert: more than one distinct axis must be seen, or a producer stamping one constant would
    // satisfy the census perfectly.
    EXPECT_GE(census.axes.size(), 2U)
        << "the census saw " << census.axes.size()
        << " distinct axis value(s): a producer stamping one constant on every entry would satisfy "
           "\"engaged\" perfectly and the field would carry no verdict. Census:\n"
        << render_census(census);
}

// invariant: compose re-derives salience over the MERGED counts, so this arm exercises a second
// admission gate rather than the first producer's output travelling through.
// invariant: both inputs are real engine documents -- a hand-built pair is exactly the population
// whose entries legitimately have no axis.
// refs: DN-64.D6
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
