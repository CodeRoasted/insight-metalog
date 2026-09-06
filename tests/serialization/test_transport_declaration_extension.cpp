// invariant: the member is emitted even when nothing was declared -- a conditionally emitted key is
// indistinguishable from a key this producer cannot emit.
// refs: ADR-23.D4, ADR-23.D6
#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

constexpr std::array kStream{
    std::string_view{
        "2026-05-31T08:00:01Z INFO request method=GET path=/api/users/1000 status=200"},
    std::string_view{"2026-05-31T08:00:02Z INFO cache key=session:1021 hit=true"},
    std::string_view{"2026-05-31T08:00:03Z WARN slow request path=/api/report latency_ms=103"},
};

using Clock = std::chrono::system_clock;

// post: the serialized bytes of one window closed with NOTHING declared.
[[nodiscard]] std::string produce_undeclared_document_json()
{
    const Clock::time_point window_start{std::chrono::seconds{1'700'000'000}};
    const Clock::time_point window_end{std::chrono::seconds{1'700'000'060}};

    meta::MetaLogConfig config;
    meta::MetaLogEngine engine{config};
    engine.open_window(window_start);

    const insight::semantic::ComposedSemantics composed{insight::semantic::compose({})};
    tok::ArenaAllocator arena{std::size_t{1} << 20};
    tok::Tokenizer tokenizer{arena, tok::MaskConfig{}, composed};
    for (const std::string_view raw : kStream)
    {
        const auto event{tokenizer.process_line(raw)};
        if (!event)
            throw std::runtime_error("transport extension: line failed to tokenize: " +
                                     std::string{raw});
        engine.ingest_event(*event);
    }

    const auto doc{engine.close_window(window_end)};
    return meta::to_json(doc, engine.registry());
}

} // namespace

TEST(TransportDeclarationExtension, TheDocumentRootCarriesTheExtensionsContainer)
{
    const std::string json{produce_undeclared_document_json()};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_TRUE((*parsed).contains("extensions"))
        << "the document root carries no `extensions` container at all, so there is nowhere for a "
           "namespaced member to live and arm ② is measuring the wrong absence:\n"
        << json;
}

// invariant: the RECORDED emission state; the assertion below reds in BOTH directions, so a
// repaired red fails exactly as a regression does.
constexpr bool kTransportMemberIsEmitted{true};

TEST(TransportDeclarationExtension, AnUndeclaredStackStillMintsTheTransportMember)
{
    const std::string json{produce_undeclared_document_json()};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;

    const bool has_extensions{(*parsed).contains("extensions")};
    const bool has_member{has_extensions &&
                          (*parsed)["extensions"].contains("fr.coderoast.transport")};

    ASSERT_EQ(has_member, kTransportMemberIsEmitted)
        << (has_member
                ? "THE PRE-REGISTERED RED IS REPAIRED — this producer now emits "
                  "`extensions[\"fr.coderoast.transport\"]`. Set kTransportMemberIsEmitted to "
                  "true in this file; the positive contract below then runs and holds it.\n"
                : "REGRESSION — the member was recorded as EMITTED and is now absent.\n"
                  "  extensions container present: ")
        << (has_member ? "" : (has_extensions ? "yes" : "no"))
        << "\n  The member must be emitted ALWAYS, with an empty `names[]` when nothing was "
           "declared. A key emitted only for a non-empty stack is indistinguishable from a key "
           "this producer cannot emit, so \"no transport declared\" and \"this producer does not "
           "carry the field\" become the same absence to every consumer — and those are different "
           "facts about the run.\n  Expected shape: {\"catalog_version\": \""
        << insight::transport::kTransportCatalogVersion << "\", \"names\": []}.\n  Document:\n"
        << json;

    if (!has_member)
        return;

    auto& member{(*parsed)["extensions"]["fr.coderoast.transport"]};

    ASSERT_TRUE(member.contains("names"))
        << "the member exists but declares no `names` array — a stack with no name list cannot be "
           "read as \"nothing was declared\", it can only be read as a malformed member:\n"
        << json;
    EXPECT_EQ(member["names"].get_array().size(), 0U)
        << "nothing was declared, so `names` must be the EMPTY array — a non-empty list here is a "
           "declaration the run never made:\n"
        << json;

    ASSERT_TRUE(member.contains("catalog_version"))
        << "the member carries names with no catalogue version. A name without its catalogue "
           "version is unresolvable by a later reader, because a row rename is a comparability "
           "event:\n"
        << json;
    EXPECT_EQ(member["catalog_version"].get<std::string>(),
              std::string{insight::transport::kTransportCatalogVersion})
        << "the emitted catalogue version is not the catalogue in force — the field is populated "
           "from something other than insight::transport::kTransportCatalogVersion:\n"
        << json;
}
