// invariant: a declared span edge rides fr.coderoast.span_edges and never a standard behavior
// member, which holds log-order adjacency alone.
// refs: ADR-24.D7, F-SRC-insight-metalog:metalog.cppm:record_span
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

[[nodiscard]] tok::CanonicalEvent make_span(std::string_view templ, std::uint64_t span_id,
                                            std::uint64_t parent_span_id)
{
    tok::CanonicalEvent event{make_event(templ)};
    event.trace.present = true;
    event.trace.is_span = true;
    event.trace.span_id = insight::SpanId{span_id};
    if (parent_span_id != 0U)
    {
        event.trace.has_parent = true;
        event.trace.parent_span_id = insight::SpanId{parent_span_id};
    }
    return event;
}

[[nodiscard]] std::string name_of(const insight::TemplateId& id)
{
    for (const std::string_view templ :
         {"root", "child", "leaf", "orphan", "a", "b", "c", "d", "e", "a1", "a2", "b1", "b2"})
        if (id == insight::template_id_of(templ))
            return std::string{templ};
    return insight::render(id);
}

[[nodiscard]] std::string dump_span_edges(const meta::MetaLogDocument& doc)
{
    if (!doc.span_edges.has_value())
        return "  (no span_edges block)\n";
    std::string out;
    for (const auto& edge : doc.span_edges->edges)
        out += "  " + name_of(edge.parent) + " -> " + name_of(edge.child) +
               " count=" + std::to_string(edge.count) + "\n";
    return out;
}

[[nodiscard]] std::string dump_top_ngrams(const meta::MetaLogDocument& doc)
{
    if (!doc.behavior.has_value())
        return "  (no behavior block)\n";
    std::string out;
    for (const auto& ngram : doc.behavior->top_ngrams)
    {
        out += "  [";
        for (const auto& id : ngram.sequence)
            out += name_of(id) + " ";
        out += "] count=" + std::to_string(ngram.count) +
               " probability=" + std::to_string(ngram.probability) + "\n";
    }
    return out;
}

[[nodiscard]] const meta::SpanEdge* find_span_edge(const meta::MetaLogDocument& doc,
                                                   std::string_view from, std::string_view to)
{
    if (!doc.span_edges.has_value())
        return nullptr;
    for (const auto& edge : doc.span_edges->edges)
        if (edge.parent == insight::template_id_of(from) &&
            edge.child == insight::template_id_of(to))
            return &edge;
    return nullptr;
}

[[nodiscard]] const meta::NGramEntry* find_ngram(const meta::MetaLogDocument& doc,
                                                 std::initializer_list<std::string_view> templs)
{
    if (!doc.behavior.has_value())
        return nullptr;
    for (const auto& ngram : doc.behavior->top_ngrams)
    {
        if (ngram.sequence.size() != templs.size())
            continue;
        if (std::ranges::equal(ngram.sequence, templs, {}, {}, [](std::string_view templ)
                               { return insight::template_id_of(templ); }))
            return &ngram;
    }
    return nullptr;
}

const insight::Timestamp kT0{std::chrono::system_clock::now()};
const insight::Timestamp kT1{kT0 + std::chrono::seconds(60)};

} // namespace

TEST(SpanEdges, ObservedParentEdgeAccountedAtClose)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
    engine.open_window(kT0);
    engine.ingest_event(make_span("child", /*span=*/2, /*parent=*/1));
    engine.ingest_event(make_span("root", /*span=*/1, /*parent=*/0));
    const auto doc{engine.close_window(kT1)};

    const meta::SpanEdge* const edge{find_span_edge(doc, "root", "child")};
    ASSERT_NE(edge, nullptr) << "the declared edge template(parent)->template(child) is absent:\n"
                             << dump_span_edges(doc);
    EXPECT_EQ(edge->count, 1U) << dump_span_edges(doc);
    ASSERT_TRUE(doc.acquisition.has_value());
    EXPECT_EQ(doc.acquisition->span_records, 2U);
    EXPECT_EQ(doc.acquisition->orphan_parent_edges, 0U);
}

// invariant: a window of span records only has no log-order adjacency, so it carries no behavior
// block: the declared edge enters no standard member.
TEST(SpanEdges, ASpanOnlyWindowCarriesNoBehaviorBlock)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
    engine.open_window(kT0);
    engine.ingest_event(make_span("root", /*span=*/1, /*parent=*/0));
    engine.ingest_event(make_span("child", /*span=*/2, /*parent=*/1));
    engine.ingest_event(make_span("leaf", /*span=*/3, /*parent=*/2));
    const auto doc{engine.close_window(kT1)};

    EXPECT_FALSE(doc.behavior.has_value())
        << "a span-only window must carry no top_ngrams, branching, dominant_path or "
           "graph_edge_count; got:\n"
        << dump_top_ngrams(doc);
    ASSERT_TRUE(doc.span_edges.has_value());
    EXPECT_EQ(doc.span_edges->edges.size(), 2U) << dump_span_edges(doc);
}

TEST(SpanEdges, UnresolvableParentIsAnOrphanNotAGuessedEdge)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
    engine.open_window(kT0);
    engine.ingest_event(make_span("orphan", /*span=*/5, /*parent=*/99));
    const auto doc{engine.close_window(kT1)};

    ASSERT_TRUE(doc.acquisition.has_value());
    EXPECT_EQ(doc.acquisition->span_records, 1U);
    EXPECT_EQ(doc.acquisition->orphan_parent_edges, 1U);
    ASSERT_TRUE(doc.span_edges.has_value())
        << "a window with span records carries the block, empty when no parent resolved";
    EXPECT_TRUE(doc.span_edges->edges.empty()) << dump_span_edges(doc);
}

TEST(SpanEdges, ANonSpanWindowOmitsTheBlock)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
    engine.open_window(kT0);
    for (const std::string_view templ : {"a", "b", "a", "b"})
        engine.ingest_event(make_event(templ));
    const auto doc{engine.close_window(kT1)};

    EXPECT_FALSE(doc.span_edges.has_value())
        << "a window with no span record has no trace substrate, so the block is absent:\n"
        << dump_span_edges(doc);
}

TEST(SpanEdges, ObservedDagHasNoInterleaveNoiseThatInferredDoes)
{
    const std::array<std::pair<std::string_view, std::string_view>, 2> declared{
        {{"a1", "a2"}, {"b1", "b2"}}};

    const auto interleave = [&](bool observed)
    {
        meta::MetaLogEngine engine{meta::MetaLogConfig{
            .top_k_size = 32, .top_ngrams_size = 64, .trace_scoping_enabled = false}};
        engine.open_window(kT0);
        const auto emit = [&](std::string_view templ, std::uint64_t span, std::uint64_t parent)
        {
            tok::CanonicalEvent event{observed ? make_span(templ, span, parent)
                                               : make_event(templ)};
            engine.ingest_event(event);
        };
        for (int i = 0; i < 50; ++i)
        {
            const std::uint64_t base{static_cast<std::uint64_t>(i) * 10U};
            emit("a1", base + 1U, 0U);
            emit("b1", base + 3U, 0U);
            emit("a2", base + 2U, base + 1U);
            emit("b2", base + 4U, base + 3U);
        }
        return engine.close_window(kT1);
    };

    const auto observed_doc{interleave(true)};
    const auto inferred_doc{interleave(false)};
    ASSERT_TRUE(observed_doc.span_edges.has_value());
    ASSERT_TRUE(inferred_doc.behavior.has_value());

    int observed_noise{0};
    for (const auto& edge : observed_doc.span_edges->edges)
        if (std::ranges::none_of(declared,
                                 [&](const auto& pair)
                                 {
                                     return edge.parent == insight::template_id_of(pair.first) &&
                                            edge.child == insight::template_id_of(pair.second);
                                 }))
            ++observed_noise;
    int inferred_noise{0};
    for (const auto& ngram : inferred_doc.behavior->top_ngrams)
        if (std::ranges::none_of(declared,
                                 [&](const auto& pair)
                                 {
                                     return ngram.sequence[0] ==
                                                insight::template_id_of(pair.first) &&
                                            ngram.sequence[1] ==
                                                insight::template_id_of(pair.second);
                                 }))
            ++inferred_noise;

    EXPECT_NE(find_span_edge(observed_doc, "a1", "a2"), nullptr) << dump_span_edges(observed_doc);
    EXPECT_NE(find_span_edge(observed_doc, "b1", "b2"), nullptr) << dump_span_edges(observed_doc);
    EXPECT_EQ(observed_noise, 0) << "the declared edges must not manufacture cross-trace edges:\n"
                                 << dump_span_edges(observed_doc);
    EXPECT_GT(inferred_noise, 0) << "the inferred global-adjacency control must show interleave "
                                    "noise for the delta to be real:\n"
                                 << dump_top_ngrams(inferred_doc);
}

TEST(SpanEdges, ObservedEdgesReplayBitIdentically)
{
    const auto run = []
    {
        meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
        engine.open_window(kT0);
        engine.ingest_event(make_span("child", 2, 1));
        engine.ingest_event(make_span("leaf", 3, 2));
        engine.ingest_event(make_span("root", 1, 0));
        return engine.close_window(kT1);
    };
    const auto first{run()};
    const auto second{run()};
    ASSERT_TRUE(first.span_edges.has_value());
    ASSERT_TRUE(second.span_edges.has_value());
    EXPECT_EQ(*first.span_edges, *second.span_edges) << "first:\n"
                                                     << dump_span_edges(first) << "second:\n"
                                                     << dump_span_edges(second);
    EXPECT_EQ(first.acquisition->span_records, second.acquisition->span_records);
    EXPECT_EQ(first.acquisition->orphan_parent_edges, second.acquisition->orphan_parent_edges);
}

// invariant: at order 3 declared edges sharing their ids with log trigrams stay out of top_ngrams,
// whose every sequence is three ids conditioned on its first two.
// refs: ADR-24.D7, DN-126.D10
TEST(SpanEdges, Order3TopNgramsHoldsLogTrigramsOnlyAndTheEdgesRideTheBlock)
{
    meta::MetaLogEngine engine{
        meta::MetaLogConfig{.top_k_size = 16, .ngram_size = 3, .top_ngrams_size = 32}};
    engine.open_window(kT0);
    engine.ingest_event(make_span("a", /*span=*/1, /*parent=*/0));
    engine.ingest_event(make_span("b", /*span=*/2, /*parent=*/1));
    engine.ingest_event(make_span("e", /*span=*/3, /*parent=*/1));
    engine.ingest_event(make_span("a", /*span=*/4, /*parent=*/0));
    engine.ingest_event(make_span("b", /*span=*/5, /*parent=*/4));
    engine.ingest_event(make_span("a", /*span=*/6, /*parent=*/0));
    engine.ingest_event(make_span("b", /*span=*/7, /*parent=*/6));
    for (const std::string_view templ : {"a", "b", "c", "a", "b", "d", "a", "b", "c"})
        engine.ingest_event(make_event(templ));
    const auto doc{engine.close_window(kT1)};
    ASSERT_TRUE(doc.behavior.has_value());
    ASSERT_EQ(doc.behavior->ngram_size, 3U);

    for (const auto& ngram : doc.behavior->top_ngrams)
        EXPECT_EQ(ngram.sequence.size(), 3U)
            << "every top_ngrams sequence is ngram_size long; a shorter one is a declared edge "
               "that leaked into the standard member:\n"
            << dump_top_ngrams(doc);

    struct ExpectedTrigram
    {
        std::initializer_list<std::string_view> sequence;
        std::string label;
        std::uint64_t count;
        double probability;
    };
    const std::array<ExpectedTrigram, 2> trigrams{{
        {{"a", "b", "c"}, "trigram (a,b,c)", 2U, 2.0 / 3.0},
        {{"a", "b", "d"}, "trigram (a,b,d)", 1U, 1.0 / 3.0},
    }};
    for (const auto& row : trigrams)
    {
        const meta::NGramEntry* const actual{find_ngram(doc, row.sequence)};
        ASSERT_NE(actual, nullptr) << row.label << " is absent from top_ngrams:\n"
                                   << dump_top_ngrams(doc);
        EXPECT_EQ(actual->count, row.count) << row.label << "\n" << dump_top_ngrams(doc);
        EXPECT_DOUBLE_EQ(actual->probability, row.probability)
            << row.label << ": actual " << actual->probability << ", expected " << row.probability
            << "\n"
            << dump_top_ngrams(doc);
    }

    struct ExpectedEdge
    {
        std::string_view parent;
        std::string_view child;
        std::uint64_t count;
    };
    const std::array<ExpectedEdge, 2> edges{{{"a", "b", 3U}, {"a", "e", 1U}}};
    ASSERT_TRUE(doc.span_edges.has_value());
    ASSERT_EQ(doc.span_edges->edges.size(), edges.size()) << dump_span_edges(doc);
    for (std::size_t index{0}; index < edges.size(); ++index)
    {
        const meta::SpanEdge& actual{doc.span_edges->edges[index]};
        EXPECT_EQ(actual.parent, insight::template_id_of(edges[index].parent))
            << "edge " << index << " out of (count desc, parent, child) order:\n"
            << dump_span_edges(doc);
        EXPECT_EQ(actual.child, insight::template_id_of(edges[index].child))
            << dump_span_edges(doc);
        EXPECT_EQ(actual.count, edges[index].count) << dump_span_edges(doc);
    }
}

// invariant: span_edges_size is a declared ranking cut, and an edge refused at max_span_edge_keys
// is an OBSERVATION counted in dropped_span_edge_observations, omitted at zero.
// refs: ADR-9.D3
TEST(SpanEdges, TheBlockIsCutToItsDeclaredSizeAndCountsRefusedObservations)
{
    const auto run = [](std::size_t span_edges_size, std::size_t max_keys)
    {
        meta::MetaLogConfig config{.top_k_size = 16};
        config.span_edges_size = span_edges_size;
        config.max_span_edge_keys = max_keys;
        meta::MetaLogEngine engine{config};
        engine.open_window(kT0);
        std::uint64_t next{1};
        const auto edge = [&](std::string_view child, int times)
        {
            for (int index{0}; index < times; ++index)
            {
                const std::uint64_t parent{next++};
                engine.ingest_event(make_span("root", parent, 0));
                engine.ingest_event(make_span(child, next++, parent));
            }
        };
        edge("a", 3);
        edge("b", 2);
        edge("c", 1);
        edge("d", 2);
        return engine.close_window(kT1);
    };

    const auto cut{run(/*span_edges_size=*/2, /*max_keys=*/64)};
    ASSERT_TRUE(cut.span_edges.has_value());
    EXPECT_EQ(cut.span_edges->span_edges_size, 2U);
    ASSERT_EQ(cut.span_edges->edges.size(), 2U) << dump_span_edges(cut);
    EXPECT_EQ(cut.span_edges->edges[0].child, insight::template_id_of("a")) << dump_span_edges(cut);
    EXPECT_EQ(cut.span_edges->edges[1].child,
              std::min(insight::template_id_of("b"), insight::template_id_of("d")))
        << "a count tie is broken by the child id's bytes:\n"
        << dump_span_edges(cut);
    EXPECT_FALSE(cut.span_edges->dropped_span_edge_observations.has_value())
        << "a ranking cut refuses no observation, so the count is omitted";

    const auto refused{run(/*span_edges_size=*/32, /*max_keys=*/2)};
    ASSERT_TRUE(refused.span_edges.has_value());
    EXPECT_EQ(refused.span_edges->edges.size(), 2U) << dump_span_edges(refused);
    ASSERT_TRUE(refused.span_edges->dropped_span_edge_observations.has_value());
    EXPECT_EQ(*refused.span_edges->dropped_span_edge_observations, 3U)
        << "the edges to c (1) and d (2) arrived after the two keys were taken: 3 observations";
}

TEST(SpanEdges, TheBlockSerializesUnderItsVendorKey)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 16, .top_ngrams_size = 16}};
    engine.open_window(kT0);
    engine.ingest_event(make_span("root", /*span=*/1, /*parent=*/0));
    engine.ingest_event(make_span("child", /*span=*/2, /*parent=*/1));
    const auto doc{engine.close_window(kT1)};
    const std::string json{written_or_fail(meta::to_json(doc, engine.registry()))};

    const std::string expected{"\"fr.coderoast.span_edges\":{\"edges\":[{\"parent\":\"" +
                               insight::render(insight::template_id_of("root")) +
                               "\",\"child\":\"" +
                               insight::render(insight::template_id_of("child")) +
                               "\",\"count\":1}],\"span_edges_size\":" +
                               std::to_string(meta::MetaLogConfig::kDefaultSpanEdgesSize) + "}"};
    EXPECT_TRUE(json.contains(expected))
        << "expected the member\n  " << expected << "\nin\n  " << json;
    EXPECT_FALSE(json.contains("\"behavior\""))
        << "a span-only window must serialize no behavior member:\n"
        << json;
}
