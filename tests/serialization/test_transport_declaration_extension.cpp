// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_transport_declaration_extension.cpp — the per-run transport declaration's EXISTENCE leg on
// the wire: `extensions["fr.coderoast.transport"]`, minted even when nothing was declared.
//
// THE PROPERTY, and why it is the existence leg and not a round trip. A key emitted only when the
// declared stack is non-empty is INDISTINGUISHABLE, to any consumer, from a key this producer does
// not know how to emit — "no transport was declared" and "this producer does not carry the field"
// collapse into the same absence. Two ADR-23 slots meet here and must not be conflated:
// `ADR-23.D4` — an absent declaration IS the empty stack (a fact about the run) — and
// `ADR-23.D6` — a missing transport ENRICHMENT stays absent rather than defaulted (a fact about
// the extracts). They govern different objects, and a
// conditionally-emitted key merges them. A test that only checks "a declared stack round-trips"
// cannot see this: it never produces the undeclared case.
//
// This is not a new argument. The egress pass paid for it days ago, where `llm_host` had to become
// unconditional for exactly this reason and an existence gate reddened and was right. The cheaper
// move is to apply the lesson than to rediscover it, which is why the UNDECLARED case is the arm
// that carries the property here.
//
// HOMING — metalog serialization grain. The subject is the bytes a produced document carries; the
// producer and the serializer are both in this package, and the input is a literal stream this file
// states itself. Nothing about the property needs the LogCraft↔InSight seam or a second package.
// The declaration's INJECTION (an eidos `PipelineConfig::transport` reaching `MetaLogConfig`) is a
// different grain across a different seam and is not this file's claim.
//
// PRE-REGISTERED RED. Arm ② encodes the target behaviour and no carrier exists yet, so it SKIPs
// rather than fails and self-flips to a hard PASS when the emission lands — no edit. Arm ① is GREEN
// today and must stay green: it proves the container ② looks inside actually exists, so ②'s red is
// attributable to the missing key and not to a missing container.
//
// Determinism: literal input bytes, fixed epoch 1'700'000'000 s, core-only composition, no RNG, no
// wall clock, no threads.

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test; // std + metalog (+ detail) + insight.canon (compose/transport)

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// A plain application stream. The core-only composition reads these unaided — no dialect vocabulary
// is involved, because this file is about a container on the document root and not about what the
// vocabulary recognised.
constexpr std::array kStream{
    std::string_view{
        "2026-05-31T08:00:01Z INFO request method=GET path=/api/users/1000 status=200"},
    std::string_view{"2026-05-31T08:00:02Z INFO cache key=session:1021 hit=true"},
    std::string_view{"2026-05-31T08:00:03Z WARN slow request path=/api/report latency_ms=103"},
};

using Clock = std::chrono::system_clock;

// Close one window with NOTHING declared — the default configuration, which is exactly the case the
// property is about. Returned as serialized bytes: the claim is about what a consumer receives, and
// a domain-object assertion would hold one level short of it.
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

// ── ① INSTRUMENT INTEGRITY — the §7 container itself is on the wire ────────────────────────────
//
// GREEN today and after the carrier lands. Arm ② asserts a key INSIDE `extensions`; if the
// container were absent from an undeclared document, ② would skip while describing a missing
// transport key, and the skip message would name the wrong defect.
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

// ── ② THE PROPERTY — an EMPTY stack still mints the key. PRE-REGISTERED RED ────────────────────
//
// The document was produced with nothing declared. The member must be present anyway, carrying an
// empty `names[]` and the catalogue version those names resolve against. The version is not
// decoration: a catalogue row rename is a comparability event, so a name recorded without its
// catalogue version is unresolvable by a later reader — and recording an unresolvable name is worse
// than recording nothing.
//
// `catalog_version` is compared against `insight::transport::kTransportCatalogVersion` rather than
// a literal on purpose. The claim is about the WIRING — that the field is populated from the
// catalogue in force — not about the catalogue's value, which canon owns and freezes. A literal
// here would be a second source of truth that goes stale at the next bump and passes anyway.
//
// Flips to green when `fr.coderoast.transport` is emitted from the document root's extensions.
TEST(TransportDeclarationExtension, AnUndeclaredStackStillMintsTheTransportMember)
{
    const std::string json{produce_undeclared_document_json()};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;

    const bool has_extensions{(*parsed).contains("extensions")};
    const bool has_member{has_extensions &&
                          (*parsed)["extensions"].contains("fr.coderoast.transport")};

    if (!has_member)
    {
        GTEST_SKIP()
            << "PRE-REGISTERED RED — a document produced with no transport declaration carries no "
               "`extensions[\"fr.coderoast.transport\"]` member.\n"
               "  extensions container present: "
            << (has_extensions ? "yes" : "no")
            << "; `fr.coderoast.transport` present: no.\n"
               "  The member must be emitted ALWAYS, with an empty `names[]` when nothing was "
               "declared. A key emitted only for a non-empty stack is indistinguishable from a key "
               "this producer cannot emit, so \"no transport declared\" and \"this producer does "
               "not carry the field\" become the same absence to every consumer — and those are "
               "different facts about the run.\n"
               "  Expected shape: {\"catalog_version\": \""
            << insight::transport::kTransportCatalogVersion
            << "\", \"names\": []}.\n"
               "  Flips to green when the carrier lands. Document:\n"
            << json;
    }

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
// NOLINTEND
