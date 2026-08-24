// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// test_run_outcome_field.cpp — the additive run-verdict scalar on MetaLogDocument.
// The additive-block discipline (a new block keeps the wire version because its ABSENCE is the
// legacy reading), applied to a plain enum field: Unknown is BOTH the in-memory default and the
// wire ABSENCE, so a verdict-free / legacy document's JSON is byte-identical to a pre-outcome
// producer's — NO metalog wire-version bump. Two faces:
//   1. ABSENCE — a default (Unknown) document emits NO run_outcome key (the additive-block proof's
//      unit leg; the byte-compare gate is the measure-first INERT run).
//   2. PRESENCE — a stamped verdict serialises as the LOWER-CASE token SPEC §2.5 mints, and
//      `unstable` stays `unstable` (never folded — the G-OUT-2 property at the wire).
// The case is the assertion, not a detail: §2.5 states the vocabulary is case-sensitive and
// `schema/metalog.v0.schema.json` pins it as a CLOSED enum, so an upper-case token fails §8
// clause 1. The Sift change report spells the same four classes UPPER-CASE for its own consumer
// (`sift-action/src/types.ts`); these two wires are deliberately not aligned, so a token from one
// is never evidence about the other.
// A diff here is a wire-contract break — fix the code, never the assertion.

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

[[nodiscard]] meta::MetaLogDocument build_doc(insight::RunOutcome outcome,
                                              meta::TemplateRegistry* out_registry)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = 8}};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    auto doc{engine.close_window(start + std::chrono::seconds(60))};
    doc.run_outcome = outcome; // the producing orchestration's stamp
    *out_registry = engine.registry();
    return doc;
}
} // namespace

// ── 1. ABSENCE: Unknown (the default) emits NO key — the wire is pre-outcome byte-identical ──
TEST(RunOutcomeField, UnknownEmitsNoKey)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc(insight::RunOutcome::Unknown, &registry)};
    const std::string json{meta::to_json(doc, registry)};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed).contains("run_outcome"))
        << "Unknown must serialise as ABSENCE (additive field, no wire bump): " << json;
}

// ── 2. PRESENCE: a stamped verdict rides the wire in SPEC §2.5's minted lower-case vocabulary ──
TEST(RunOutcomeField, StampedVerdictSerialises)
{
    const std::array<std::pair<insight::RunOutcome, std::string_view>, 4> cases{{
        {insight::RunOutcome::Success, "success"},
        {insight::RunOutcome::Failure, "failure"},
        {insight::RunOutcome::Unstable, "unstable"},
        {insight::RunOutcome::Aborted, "aborted"},
    }};
    for (const auto& [outcome, expected] : cases)
    {
        meta::TemplateRegistry registry;
        const auto doc{build_doc(outcome, &registry)};
        const std::string json{meta::to_json(doc, registry)};
        const auto parsed{glz::read_json<glz::generic>(json)};
        ASSERT_TRUE(parsed.has_value()) << json;
        ASSERT_TRUE((*parsed).contains("run_outcome")) << "expected " << expected << ": " << json;
        EXPECT_EQ((*parsed)["run_outcome"].get<std::string>(), expected)
            << "the verdict must ride in SPEC §2.5's lower-case vocabulary and unfolded "
               "(`unstable` is never collapsed into `failure`); got "
            << (*parsed)["run_outcome"].get<std::string>() << " in: " << json;
    }
}
// NOLINTEND
