// invariant: RunOutcome::Unknown is both the in-memory default and the wire ABSENCE, so a
// verdict-free document is byte-identical to a pre-outcome producer's.
// refs: F-SRC-metalog-spec:SPEC.md, F-SRC-metalog-spec:metalog.v0.schema.json
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
    doc.run_outcome = outcome;
    *out_registry = engine.registry();
    return doc;
}
} // namespace

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
