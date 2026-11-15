// refs: F-SRC-metalog-spec:SPEC.md, ADR-17.D3
// invariant: the composed identity of the semantic packages in force rides the SPEC 2.4
// canonicalization_version, folded at the engine seam: two compositions carry two tokens, refused.
// invariant: the vendor block extensions["fr.coderoast.ruleset"] keeps the package list alone, a
// LABEL that gates nothing, carried by compose() only when both inputs carry an equal list.
// note: a diff here is a comparability-contract break: fix the code, never the assertion.
#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

const meta::RulesetIdentity kRulesetA{
    .packages = {{.name = "github", .version = "1.0.0"},
                 {.name = "test_frameworks", .version = "1.0.0"}}};
const meta::RulesetIdentity kRulesetB{
    .packages = {{.name = "github", .version = "2.0.0"},
                 {.name = "test_frameworks", .version = "1.0.0"}}};
// note: two folded tokens under one masking generation, standing for two compositions.
const std::string kTokenA{std::string{insight::kCanonicalizationVersion} +
                          ":a1b2c3d4e5f60718a1b2c3d4e5f60718"};
const std::string kTokenB{std::string{insight::kCanonicalizationVersion} +
                          ":ffffffffffffffffffffffffffffffff"};

// invariant: every other processing identifier stays at its default, so the token and the label
// are the variables across the gate.
[[nodiscard]] meta::MetaLogDocument build_doc(std::optional<meta::RulesetIdentity> ruleset,
                                              std::string token = kTokenA,
                                              meta::TemplateRegistry* out_registry = nullptr)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.ruleset = std::move(ruleset);
    cfg.canonicalization_version = std::move(token);
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    auto doc{engine.close_window(start + std::chrono::seconds(60))};
    if (out_registry != nullptr)
        *out_registry = engine.registry();
    return doc;
}
} // namespace

// invariant: the block rides the SPEC 7 container, reads back via a generic parser, and carries
// the package list ONLY -- the identity it used to carry is the 2.4 token's now.
TEST(RulesetIdentity, TheBlockRoundTripsThePackageListOnly)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc(kRulesetA, kTokenA, &registry)};
    const std::string json{written_or_fail(meta::to_json(doc, registry))};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed).contains("ruleset"))
        << "SPEC §7: the ruleset block is vendor data and must never be a BARE root member: "
        << json;
    ASSERT_TRUE((*parsed).contains("extensions")) << json;
    ASSERT_TRUE((*parsed)["extensions"].contains("fr.coderoast.ruleset")) << json;

    auto& ruleset{(*parsed)["extensions"]["fr.coderoast.ruleset"]};
    EXPECT_FALSE(ruleset.contains("semantic_identity"))
        << "ADR-17.D3: semantic_identity left the wire; the 2.4 token carries the identity: "
        << json;
    ASSERT_TRUE(ruleset.contains("packages")) << json;
    auto& packages{ruleset["packages"].get_array()};
    ASSERT_EQ(packages.size(), 2U) << json;
    EXPECT_EQ(packages[0]["name"].get<std::string>(), "github") << json;
    EXPECT_EQ(packages[0]["version"].get<std::string>(), "1.0.0") << json;
    EXPECT_EQ(packages[1]["name"].get<std::string>(), "test_frameworks") << json;
    EXPECT_EQ((*parsed)["canonicalization_version"].get<std::string>(), kTokenA) << json;
}

// invariant: a producer that injects no composition emits NO block.
TEST(RulesetIdentity, AProducerInjectingNoCompositionEmitsNoBlock)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc(std::nullopt, kTokenA, &registry)};
    EXPECT_FALSE(doc.ruleset.has_value());

    const std::string json{written_or_fail(meta::to_json(doc, registry))};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed).contains("ruleset")) << json;
    // invariant: the container itself may exist, the engine stamping a transport declaration on
    // every document, so the assertion is on the ruleset KEY and not on the container.
    if ((*parsed).contains("extensions"))
    {
        EXPECT_FALSE((*parsed)["extensions"].contains("fr.coderoast.ruleset")) << json;
    }
}

// invariant: two compositions' documents carry two tokens, and the ONE 2.4 gate refuses them in
// both operations -- re-segment or refuse, never a silent compare.
TEST(RulesetIdentity, TwoCompositionsAreRefusedThroughTheSection24Gate)
{
    const auto lhs{build_doc(kRulesetA, kTokenA)};
    const auto rhs{build_doc(kRulesetB, kTokenB)};
    EXPECT_THROW((void)meta::compose(lhs, rhs), std::invalid_argument)
        << "two folded tokens must refuse compose(): " << kTokenA << " vs " << kTokenB;
    EXPECT_THROW((void)meta::diff(lhs, rhs), std::invalid_argument)
        << "two folded tokens must refuse diff(): " << kTokenA << " vs " << kTokenB;
}

// invariant: the label gates nothing -- equal tokens with different lists compose and diff, and
// the composed document omits the list rather than pick one.
TEST(RulesetIdentity, TheLabelGatesNothingAndIsCarriedOnlyWhenEqual)
{
    const auto lhs{build_doc(kRulesetA)};
    const auto rhs{build_doc(kRulesetB)};
    EXPECT_NO_THROW((void)meta::diff(lhs, rhs));
    EXPECT_FALSE(meta::compose(lhs, rhs).ruleset.has_value())
        << "two different lists under one token: the composed document carries neither";
    EXPECT_FALSE(meta::compose(rhs, lhs).ruleset.has_value());

    const auto same{meta::compose(lhs, build_doc(kRulesetA))};
    ASSERT_TRUE(same.ruleset.has_value()) << "an equal list on both inputs is carried";
    EXPECT_EQ(*same.ruleset, kRulesetA);

    EXPECT_FALSE(meta::compose(lhs, build_doc(std::nullopt)).ruleset.has_value())
        << "a one-sided list is not carried: it describes one input under a window covering both";
}
