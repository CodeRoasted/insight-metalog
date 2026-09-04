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
/// THE RECORDED STATE of what was a pre-registered red — now `true`: the producer DOES emit
/// `extensions["fr.coderoast.transport"]`, so the positive contract in the case below runs and
/// holds it. Flip it back only if the member is deliberately withdrawn.
///
/// **THE RED HAD ALREADY BEEN REPAIRED AND NOTHING SAID SO.** Until 2026-09-04 this case opened
/// with a `GTEST_SKIP` describing the missing member, and a gtest skip exits 0 — so once the
/// carrier actually landed, the case went on printing a registered red and counting as a pass,
/// and the positive contract below had never run against a producer that satisfies it. Replacing
/// the skip with an assertion against this constant surfaced the repair on the FIRST run: the
/// document carries `{"catalog_version": "transport-catalog-3", "names": []}`, which is the shape
/// the old skip message said to expect. That is the whole argument for the shape, measured.
constexpr bool kTransportMemberIsEmitted{true};

// Flips to green when `fr.coderoast.transport` is emitted from the document root's extensions.
TEST(TransportDeclarationExtension, AnUndeclaredStackStillMintsTheTransportMember)
{
    const std::string json{produce_undeclared_document_json()};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;

    const bool has_extensions{(*parsed).contains("extensions")};
    const bool has_member{has_extensions &&
                          (*parsed)["extensions"].contains("fr.coderoast.transport")};

    // PRE-REGISTERED RED, PINNED RATHER THAN SKIPPED (Founder, 2026-09-04). This branch used to
    // `GTEST_SKIP` with the diagnosis below, on the reasoning that a red should be REGISTERED
    // without blocking the cut. The reasoning holds; the mechanism did not. **A gtest skip exits
    // 0 and ctest counts it as passed**, so the registered red was reported as a passing test and
    // the pass count covered a contract the producer does not honour.
    //
    // `kTransportMemberIsEmitted` is the RECORDED state, and comparing reality against it is a
    // real subject rather than a tautology: it reds in BOTH directions. While the red stands the
    // case passes having asserted something true; the day the producer starts emitting the member
    // it FAILS, which is the signal to flip the constant and let the positive contract below run.
    // Nothing is blocked in the meantime, which was the whole point of the skip.
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

    // The red still stands, pinned by the assertion above. Everything below is the POSITIVE
    // contract and is the flip's target — reachable, never dead, the moment the producer emits.
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
