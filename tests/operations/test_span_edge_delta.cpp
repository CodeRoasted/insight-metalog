// invariant: the span-edge delta is its own diff pass over the two span_edges blocks, keyed
// (parent, child), present only when both sides carry the block and an edge appeared or vanished.
// refs: ADR-24.D7, F-SRC-insight-metalog:metalog.api.cppm:SpanEdgeDelta
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace meta = insight::metalog;

[[nodiscard]] insight::TemplateId tid(std::string_view templ)
{
    return insight::template_id_of(templ);
}

[[nodiscard]] meta::MetaLogDocument with_edges(std::vector<meta::SpanEdge> edges)
{
    meta::MetaLogDocument doc;
    doc.window.envelope = meta::WindowEnvelope{
        .bounds = {.start_iso = "2026-01-01T00:00:00Z", .end_iso = "2026-01-01T00:01:00Z"},
        .duration_seconds = 60};
    doc.span_edges = meta::SpanEdgeBlock{
        .edges = std::move(edges), .span_edges_size = meta::MetaLogConfig::kDefaultSpanEdgesSize};
    return doc;
}

[[nodiscard]] std::string dump(const std::vector<meta::SpanEdge>& edges)
{
    std::string out;
    for (const auto& edge : edges)
        out += "  " + insight::render(edge.parent) + " -> " + insight::render(edge.child) +
               " count=" + std::to_string(edge.count) + "\n";
    return out.empty() ? "  (none)\n" : out;
}

} // namespace

TEST(SpanEdgeDelta, NewAndVanishedEdgesCarryTheirSideCountSortedByKey)
{
    const auto previous{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 4},
                                    {.parent = tid("root"), .child = tid("gone"), .count = 2}})};
    const auto current{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 4},
                                   {.parent = tid("root"), .child = tid("z"), .count = 1},
                                   {.parent = tid("root"), .child = tid("b"), .count = 7}})};
    const auto delta{meta::diff(previous, current)};
    ASSERT_TRUE(delta.span_edge_delta.has_value());

    std::vector<meta::SpanEdge> expected_new{
        {.parent = tid("root"), .child = tid("z"), .count = 1},
        {.parent = tid("root"), .child = tid("b"), .count = 7}};
    std::ranges::sort(expected_new, {}, [](const meta::SpanEdge& edge) { return edge.child; });
    EXPECT_EQ(delta.span_edge_delta->new_edges, expected_new)
        << "actual:\n"
        << dump(delta.span_edge_delta->new_edges) << "expected:\n"
        << dump(expected_new);
    const std::vector<meta::SpanEdge> expected_vanished{
        {.parent = tid("root"), .child = tid("gone"), .count = 2}};
    EXPECT_EQ(delta.span_edge_delta->vanished_edges, expected_vanished)
        << "actual:\n"
        << dump(delta.span_edge_delta->vanished_edges);
}

TEST(SpanEdgeDelta, ACountOnlyMoveMintsNoDelta)
{
    const auto previous{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 4}})};
    const auto current{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 9}})};
    const auto delta{meta::diff(previous, current)};
    EXPECT_FALSE(delta.span_edge_delta.has_value())
        << "the declared edge set did not move; new:\n"
        << dump(delta.span_edge_delta->new_edges) << "vanished:\n"
        << dump(delta.span_edge_delta->vanished_edges);
}

TEST(SpanEdgeDelta, AbsentWhenEitherSideCarriesNoBlock)
{
    const auto spans{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 4}})};
    meta::MetaLogDocument plain{spans};
    plain.span_edges.reset();
    EXPECT_FALSE(meta::diff(plain, spans).span_edge_delta.has_value())
        << "a side with no trace substrate makes every edge verdict unknown, never all-new";
    EXPECT_FALSE(meta::diff(spans, plain).span_edge_delta.has_value())
        << "a side with no trace substrate makes every edge verdict unknown, never all-vanished";
}

// invariant: comparison_outcome is derived from the STANDARD signal properties only, so a vendor
// delta alone never witnesses a change.
// refs: F-SRC-insight-metalog:metalog.cppm:comparison_outcome_of
TEST(SpanEdgeDelta, AVendorDeltaAloneLeavesTheStandardOutcomeUnchanged)
{
    const auto previous{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 1}})};
    const auto current{with_edges({{.parent = tid("root"), .child = tid("b"), .count = 1}})};
    const auto delta{meta::diff(previous, current)};
    ASSERT_TRUE(delta.span_edge_delta.has_value());
    EXPECT_EQ(meta::comparison_outcome_of(delta), meta::ComparisonOutcome::Unchanged);
}

TEST(SpanEdgeDelta, TheDeltaSerializesUnderTheDiffExtensions)
{
    const auto previous{with_edges({{.parent = tid("root"), .child = tid("a"), .count = 3}})};
    const auto current{with_edges({{.parent = tid("root"), .child = tid("b"), .count = 2}})};
    const std::string json{written_or_fail(meta::to_json(meta::diff(previous, current)))};
    const std::string expected{
        "\"extensions\":{\"fr.coderoast.span_edge_delta\":{\"new_edges\":[{\"parent\":\"" +
        insight::render(tid("root")) + "\",\"child\":\"" + insight::render(tid("b")) +
        "\",\"count\":2}],\"vanished_edges\":[{\"parent\":\"" + insight::render(tid("root")) +
        "\",\"child\":\"" + insight::render(tid("a")) + "\",\"count\":3}]}}"};
    EXPECT_TRUE(json.contains(expected)) << "expected\n  " << expected << "\nin\n  " << json;
}
