// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// test_run_outcome_field.cpp — the ADR-17 additive run-verdict scalar on MetaLogDocument.
// The additive-block discipline ([[additive-gated-metalog-block-keeps-wire-version]]), applied to a
// plain enum field: Unknown is BOTH the in-memory default and the wire ABSENCE, so a verdict-free /
// legacy document's JSON is byte-identical to a pre-outcome producer's — NO metalog wire-version
// bump. Two faces:
//   1. ABSENCE — a default (Unknown) document emits NO run_outcome key (the additive-block proof's
//      unit leg; the byte-compare gate is the measure-first INERT run).
//   2. PRESENCE — a stamped verdict serialises as the canonical UPPERCASE category string, and
//      UNSTABLE stays UNSTABLE (never folded — the G-OUT-2 property at the wire).
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
    doc.run_outcome = outcome; // the producing orchestration's stamp (ADR-17)
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

// ── 2. PRESENCE: a stamped verdict rides the wire as its canonical category string ──
TEST(RunOutcomeField, StampedVerdictSerialises)
{
    const std::array<std::pair<insight::RunOutcome, std::string_view>, 4> cases{{
        {insight::RunOutcome::Success, "SUCCESS"},
        {insight::RunOutcome::Failure, "FAILURE"},
        {insight::RunOutcome::Unstable, "UNSTABLE"},
        {insight::RunOutcome::Aborted, "ABORTED"},
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
            << "the verdict must ride verbatim (UNSTABLE is never folded): " << json;
    }
}
// NOLINTEND
