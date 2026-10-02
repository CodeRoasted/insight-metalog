// refs: F-SRC-insight-metalog:metalog.cppm:record_span
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

import insight.metalog.test;

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

[[nodiscard]] bool has_edge(const meta::MetaLogDocument& doc, std::string_view from,
                            std::string_view to)
{
    if (!doc.behavior.has_value())
        return false;
    for (const auto& ngram : doc.behavior->top_ngrams)
        if (ngram.sequence.size() == 2 && ngram.sequence[0] == insight::template_id_of(from) &&
            ngram.sequence[1] == insight::template_id_of(to))
            return true;
    return false;
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

    EXPECT_TRUE(has_edge(doc, "root", "child")) << "observed edge template(parent)→template(child)";
    ASSERT_TRUE(doc.acquisition.has_value());
    EXPECT_EQ(doc.acquisition->span_records, 2U);
    EXPECT_EQ(doc.acquisition->orphan_parent_edges, 0U);
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
    EXPECT_FALSE(has_edge(doc, "orphan", "orphan"));
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
    ASSERT_TRUE(observed_doc.behavior.has_value());
    ASSERT_TRUE(inferred_doc.behavior.has_value());

    const auto noise_edges = [&](const meta::MetaLogDocument& doc)
    {
        int noise{0};
        for (const auto& ngram : doc.behavior->top_ngrams)
        {
            if (ngram.sequence.size() != 2)
                continue;
            const bool is_declared{std::ranges::any_of(
                declared,
                [&](const auto& edge)
                {
                    return ngram.sequence[0] == insight::template_id_of(edge.first) &&
                           ngram.sequence[1] == insight::template_id_of(edge.second);
                })};
            if (!is_declared)
                ++noise;
        }
        return noise;
    };

    EXPECT_TRUE(has_edge(observed_doc, "a1", "a2"));
    EXPECT_TRUE(has_edge(observed_doc, "b1", "b2"));
    EXPECT_EQ(noise_edges(observed_doc), 0)
        << "observed DAG must not manufacture cross-trace edges";
    EXPECT_GT(noise_edges(inferred_doc), 0) << "the inferred global-adjacency control must show "
                                               "interleave noise for the delta to be real";
}

TEST(SpanEdges, ObservedGraphReplaysBitIdentically)
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
    ASSERT_TRUE(first.behavior.has_value());
    ASSERT_TRUE(second.behavior.has_value());
    ASSERT_EQ(first.behavior->top_ngrams.size(), second.behavior->top_ngrams.size());
    for (std::size_t i = 0; i < first.behavior->top_ngrams.size(); ++i)
        EXPECT_EQ(first.behavior->top_ngrams[i].sequence, second.behavior->top_ngrams[i].sequence)
            << "observed n-gram " << i << " diverged across replays";
    EXPECT_EQ(first.acquisition->span_records, second.acquisition->span_records);
    EXPECT_EQ(first.acquisition->orphan_parent_edges, second.acquisition->orphan_parent_edges);
}

// invariant: at order 3 a span edge (two ids) sits beside log trigrams (three ids), and each
// sequence's probability conditions on its own first size - 1 ids among sequences of its length.
// refs: DN-126.D10
TEST(SpanEdges, Order3ProbabilityConditionsOnPrefixOfItsOwnLength)
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

    struct Expected
    {
        std::initializer_list<std::string_view> sequence;
        std::string label;
        std::uint64_t count;
        double probability;
    };
    const std::array<Expected, 4> expected{{
        {{"a", "b"}, "span edge (a,b)", 3U, 3.0 / 4.0},
        {{"a", "e"}, "span edge (a,e)", 1U, 1.0 / 4.0},
        {{"a", "b", "c"}, "trigram (a,b,c)", 2U, 2.0 / 3.0},
        {{"a", "b", "d"}, "trigram (a,b,d)", 1U, 1.0 / 3.0},
    }};

    const auto name_of = [](const insight::TemplateId& id) -> std::string
    {
        for (const std::string_view templ : {"a", "b", "c", "d", "e"})
            if (id == insight::template_id_of(templ))
                return std::string{templ};
        return "?";
    };
    std::string dump;
    for (const auto& ngram : doc.behavior->top_ngrams)
    {
        dump += "  [";
        for (const auto& id : ngram.sequence)
            dump += name_of(id) + " ";
        dump += "] count=" + std::to_string(ngram.count) +
                " probability=" + std::to_string(ngram.probability) + "\n";
    }
    for (const auto& row : expected)
    {
        const meta::NGramEntry* const actual{find_ngram(doc, row.sequence)};
        ASSERT_NE(actual, nullptr) << row.label << " is absent from top_ngrams:\n" << dump;
        EXPECT_EQ(actual->count, row.count) << row.label << "\n" << dump;
        EXPECT_DOUBLE_EQ(actual->probability, row.probability)
            << row.label << ": actual " << actual->probability << ", expected " << row.probability
            << " (count over the summed count of the sequences of the SAME "
            << "length sharing its first size - 1 ids)\n"
            << dump;
    }
}
