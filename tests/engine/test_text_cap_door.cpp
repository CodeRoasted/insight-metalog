// invariant: DN-148.D5's text door: a retained value or component is itself below L bytes, else
// an exactly-L-byte digest form, and the document declares L.
// invariant: each pinned form's oracle is an independent model (Python hashlib SHA-256 and its
// maximal-subpart UTF-8 replacement), never cap_text, so a green says two producers agree.
// refs: DN-148.D5
#include <gtest/gtest.h>
#include <utf8/well_formed.hpp>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// post: the bytes with every byte outside printable ASCII as \xHH, so a failure shows which differ.
[[nodiscard]] std::string bytes_of(std::string_view text)
{
    std::string out;
    for (const char byte : text)
    {
        const auto code{static_cast<unsigned char>(byte)};
        if (code >= 0x20U && code < 0x7FU)
            out.push_back(byte);
        else
            out += std::format("\\x{:02X}", code);
    }
    return out;
}

struct PinnedForm
{
    std::string_view value;
    std::size_t max_text_bytes;
    std::string_view form;
};

// invariant: computed by the independent model named in this file's header, never by cap_text.
constexpr std::array<PinnedForm, 7> kPinnedForms{{
    {.value = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
     .max_text_bytes = 64,
     .form = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    {.value = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
     .max_text_bytes = 64,
     .form = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa~ffe054fe7ae0cb6d"},
    {.value = "550e8400-e29b-41d4-a716-446655440000",
     .max_text_bytes = 32,
     .form = "550e8400-e29b-4~a3a9e1ed9732cab2"},
    {.value = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\xE2\x82\xAC"
              "tailtailtailtailtailtailtailtail",
     .max_text_bytes = 64,
     .form = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx~~f86fa813d794ab17"},
    {.value = "bad\xFF\xFEzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
     .max_text_bytes = 64,
     .form = "bad\xEF\xBF\xBD\xEF\xBF\xBDzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz~8f85f0d5d0c0f62c"},
    {.value = "\xE2\x82", .max_text_bytes = 32, .form = "\xEF\xBF\xBD"},
    {.value = "component.with.a.very.long.dotted.name.that.passes.sixty-four.bytes",
     .max_text_bytes = 64,
     .form = "component.with.a.very.long.dotted.name.that.pas~de88d5a339ccdd73"},
}};

// post: an ASCII value of `length` bytes whose bytes vary along it, filled with `seed`.
[[nodiscard]] std::string long_value(std::size_t length, char seed)
{
    std::string out(length, seed);
    for (std::size_t index{0}; index < length; index += 7)
        out[index] = static_cast<char>('a' + ((index / 7) % 26));
    return out;
}

[[nodiscard]] std::chrono::system_clock::time_point window_start()
{
    return std::chrono::system_clock::time_point{} + std::chrono::hours{1};
}

} // namespace

TEST(TextCapDoor, EveryPinnedValueMapsToTheIndependentModelsForm)
{
    for (const PinnedForm& pinned : kPinnedForms)
    {
        const std::string form{meta::cap_text(pinned.value, pinned.max_text_bytes)};
        EXPECT_EQ(form, pinned.form)
            << "cap_text(\"" << bytes_of(pinned.value) << "\", " << pinned.max_text_bytes
            << ") gave \"" << bytes_of(form) << "\" (" << form.size()
            << " bytes); the independent model's form is \"" << bytes_of(pinned.form) << "\" ("
            << pinned.form.size() << " bytes)";
        EXPECT_LE(form.size(), pinned.max_text_bytes)
            << "a capped member is never longer than L: \"" << bytes_of(form) << "\"";
    }
}

TEST(TextCapDoor, TheBoundaryIsLMinusOneVerbatimAndLMapped)
{
    for (std::size_t cap{meta::kMinTextBytes}; cap <= meta::kMaxTextBytes; cap *= 2)
    {
        const std::string below(cap - 1, 'q');
        const std::string at(cap, 'q');
        EXPECT_EQ(meta::cap_text(below, cap), below)
            << "at L = " << cap << " a value of L - 1 bytes is kept verbatim";
        const std::string mapped{meta::cap_text(at, cap)};
        EXPECT_EQ(mapped.size(), cap) << "at L = " << cap << " a value of L bytes maps to an "
                                      << "exactly-L-byte digest form, got " << bytes_of(mapped);
        EXPECT_NE(mapped, at) << "at L = " << cap << " a value of exactly L bytes is mapped, so "
                              << "length alone recognises a digest form";
        EXPECT_TRUE(meta::is_text_digest_form(mapped, cap));
        EXPECT_FALSE(meta::is_text_digest_form(below, cap));
    }
}

TEST(TextCapDoor, TwoValuesSharingTheKeptPrefixStayDistinct)
{
    constexpr std::size_t kCap{64};
    const std::string shared(kCap - 17, 's');
    const std::string first{shared + "-first-tail-of-the-value"};
    const std::string second{shared + "-second-tail-of-the-value"};
    const std::string first_form{meta::cap_text(first, kCap)};
    const std::string second_form{meta::cap_text(second, kCap)};
    EXPECT_EQ(first_form.substr(0, kCap - 17), second_form.substr(0, kCap - 17))
        << "fixture: both forms keep the same " << (kCap - 17) << "-byte prefix";
    EXPECT_NE(first_form, second_form)
        << "two values sharing their first L - 17 bytes must stay two members, the digest "
           "separating them: \""
        << first_form << "\" vs \"" << second_form << "\"";
}

TEST(TextCapDoor, ACodePointStraddlingTheCutLengthensTheTildeRun)
{
    constexpr std::size_t kCap{32};
    // note: L - 17 = 15 bytes of prefix room; a 4-byte code point at byte 13 straddles the cut.
    const std::string value{std::string(13, 'p') + "\xF0\x9D\x84\x9E" + std::string(40, 'r')};
    const std::string form{meta::cap_text(value, kCap)};
    ASSERT_EQ(form.size(), kCap) << bytes_of(form);
    EXPECT_EQ(form.substr(0, 13), std::string(13, 'p'))
        << "the prefix ends before the straddling code point: " << bytes_of(form);
    EXPECT_EQ(form.substr(13, kCap - 16 - 13), std::string(kCap - 16 - 13, '~'))
        << "the tilde run fills the bytes the code point would have half-occupied: "
        << bytes_of(form);
    EXPECT_EQ(insight::utf8::well_formed_prefix(form), form.size())
        << "the form is well-formed UTF-8: " << bytes_of(form);
}

TEST(TextCapDoor, AnIllFormedValueIsReplacedBeforeCapping)
{
    const std::string short_ill_formed{"id\xC3"};
    EXPECT_EQ(meta::cap_text(short_ill_formed, 32), "id\xEF\xBF\xBD")
        << "a short ill-formed value is well-formed, then kept: "
        << bytes_of(meta::cap_text(short_ill_formed, 32));
}

TEST(TextCapDoor, ADigestFormDisplaysAsItsPrefixAndAnExplicitMarker)
{
    const std::string form{meta::cap_text(std::string(80, 'w'), 64)};
    EXPECT_EQ(meta::text_display_form(form, 64), std::string(47, 'w') + "\xE2\x80\xA6")
        << "a digest form shows its prefix, tildes removed, then U+2026; never as an observed "
           "value: "
        << bytes_of(meta::text_display_form(form, 64));
    EXPECT_EQ(meta::text_display_form("short", 64), "short");
    EXPECT_EQ(meta::text_display_form(form, std::nullopt), form)
        << "a document declaring no cap has no digest form";
}

TEST(TextCapDoor, AnUndeclarableCapIsRefusedNamingTheKey)
{
    for (const std::size_t refused :
         {std::size_t{0}, std::size_t{16}, std::size_t{48}, std::size_t{100}, std::size_t{8192}})
    {
        try
        {
            const meta::MetaLogEngine engine{meta::MetaLogConfig{.max_text_bytes = refused}};
            ADD_FAILURE() << "MetaLogEngine accepted max_text_bytes = " << refused
                          << ", which is not a power of two in [32, 4096]";
        }
        catch (const std::invalid_argument& error)
        {
            EXPECT_NE(std::string_view{error.what()}.find("max_text_bytes"), std::string_view::npos)
                << "the refusal must name the key: " << error.what();
        }
    }
}

TEST(TextCapDoor, AWindowRetainsEveryValueAndComponentThroughTheDoor)
{
    constexpr std::size_t kCap{meta::MetaLogConfig::kDefaultMaxTextBytes};
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_param_histograms = 1}};
    const std::string long_param{long_value(200, 'v')};
    const std::string long_component{long_value(90, 'c')};
    engine.open_window(window_start());
    for (int repeat{0}; repeat < 3; ++repeat)
    {
        auto event{ParamEvent::make("charge id=<*>", {long_param, "short"})};
        event.event.component = long_component;
        engine.ingest_event(event.event);
    }
    const meta::MetaLogDocument doc{engine.close_window(window_start() + std::chrono::seconds{1})};

    EXPECT_EQ(doc.max_text_bytes, std::optional<std::size_t>{kCap})
        << "every raw document declares the L its members were admitted under";
    ASSERT_EQ(doc.stats.top_k.size(), 1U);
    const meta::TopKEntry& entry{doc.stats.top_k[0]};
    ASSERT_EQ(entry.field_histograms.size(), 1U);
    const std::string expected_value{meta::cap_text(long_param, kCap)};
    EXPECT_EQ(entry.field_histograms[0].value_counts.size(), 1U);
    EXPECT_TRUE(entry.field_histograms[0].value_counts.contains(expected_value))
        << "a 200-byte parameter is retained as its " << kCap << "-byte digest form \""
        << expected_value << "\"; the slot holds "
        << (entry.field_histograms[0].value_counts.empty()
                ? std::string{"nothing"}
                : bytes_of(entry.field_histograms[0].value_counts.begin()->first));
    const std::string expected_component{meta::cap_text(long_component, kCap)};
    EXPECT_EQ(entry.dominant_component, std::optional<std::string>{expected_component})
        << "the component passes the same door: got "
        << bytes_of(entry.dominant_component.value_or("<none>"));
    ASSERT_TRUE(doc.has_cube);
    for (const meta::CubeCell& cell : doc.cube.cells)
        if (cell.coord.where)
            for (const std::string& label : *cell.coord.where)
                EXPECT_LE(label.size(), kCap) << "a cube WHERE label past L: " << bytes_of(label);
    for (const std::string& label : doc.cube.base_component_dict)
        EXPECT_EQ(label, expected_component) << "the cube's dictionary holds the mapped label";
    EXPECT_EQ(entry.field_histograms[0].approximate_cardinality, 1U)
        << "the sketch still counts one distinct value";
}

TEST(TextCapDoor, AServiceEdgeEndpointPassesTheDoor)
{
    constexpr std::size_t kCap{32};
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_text_bytes = kCap}};
    const std::string caller{long_value(70, 'p')};
    const std::string callee{long_value(70, 'k')};
    engine.open_window(window_start());
    auto parent{meta::test::make_event("parent span")};
    parent.component = caller;
    parent.trace.present = true;
    parent.trace.is_span = true;
    parent.trace.trace_id = insight::TraceId{7};
    parent.trace.span_id = insight::SpanId{1};
    engine.ingest_event(parent);
    auto child{meta::test::make_event("child span")};
    child.component = callee;
    child.trace.present = true;
    child.trace.is_span = true;
    child.trace.trace_id = insight::TraceId{7};
    child.trace.span_id = insight::SpanId{2};
    child.trace.has_parent = true;
    child.trace.parent_span_id = insight::SpanId{1};
    engine.ingest_event(child);
    const meta::MetaLogDocument doc{engine.close_window(window_start() + std::chrono::seconds{1})};
    ASSERT_TRUE(doc.service_edges.has_value()) << "fixture: a span window carries the block";
    ASSERT_EQ(doc.service_edges->edges.size(), 1U);
    EXPECT_EQ(doc.service_edges->edges[0].caller, meta::cap_text(caller, kCap))
        << "caller retained as " << bytes_of(doc.service_edges->edges[0].caller);
    EXPECT_EQ(doc.service_edges->edges[0].callee, meta::cap_text(callee, kCap))
        << "callee retained as " << bytes_of(doc.service_edges->edges[0].callee);
}
