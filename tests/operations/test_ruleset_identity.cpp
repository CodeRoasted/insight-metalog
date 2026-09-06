
// refs: F-SRC-metalog-spec:SPEC.md
// invariant: the semantic packages in force are content-hashed into one semantic_identity that
// rides every artifact, and two artifacts are comparable only when it matches.
// invariant: enforcement is CENTRALIZED in the processing-identifier gate shared by compose() and
// diff(), so semantic_identity is just another processing identifier through that one gate.
// invariant: three faces -- ROUND-TRIP through the vendor-namespaced extensions container read back
// generically, ABSENCE tolerated for a legacy producer, and MISMATCH failing closed.
// note: a diff here is a comparability-contract break: fix the code, never the assertion.
#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

const meta::RulesetIdentity kRulesetA{
    .semantic_identity = "a1b2c3d4e5f60718",
    .packages = {{.name = "github", .version = "1.0.0"},
                 {.name = "test_frameworks", .version = "1.0.0"}}};
const meta::RulesetIdentity kRulesetB{
    // note: a DIFFERENT composition, standing for a bumped package.
    .semantic_identity = "ffffffffffffffff",
    .packages = {{.name = "github", .version = "2.0.0"},
                 {.name = "test_frameworks", .version = "1.0.0"}}};

// invariant: every other processing identifier stays at its default, so ruleset is the isolated
// variable across the gate.
[[nodiscard]] meta::MetaLogDocument
build_doc_with_ruleset(std::optional<meta::RulesetIdentity> ruleset,
                       meta::TemplateRegistry* out_registry = nullptr)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.ruleset = std::move(ruleset);
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    auto doc{engine.close_window(start + std::chrono::seconds(60))};
    if (out_registry != nullptr)
        *out_registry = engine.registry();
    return doc;
}
} // namespace

// note: stamping -- config.ruleset rides onto the document.
TEST(RulesetIdentity, StampedFromConfigOnDocument)
{
    const auto doc{build_doc_with_ruleset(kRulesetA)};
    ASSERT_TRUE(doc.ruleset.has_value());
    EXPECT_EQ(doc.ruleset->semantic_identity, "a1b2c3d4e5f60718");
    ASSERT_EQ(doc.ruleset->packages.size(), 2U);
    EXPECT_EQ(doc.ruleset->packages[0].name, "github");
    EXPECT_EQ(doc.ruleset->packages[1].name, "test_frameworks");
}

// invariant: the block rides the SPEC 7 container and reads back via a generic parser, there being
// no typed one; inside the block the member name IS the JSON key.
TEST(RulesetIdentity, RoundTripsThroughGenericJson)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc_with_ruleset(kRulesetA, &registry)};
    const std::string json{meta::to_json(doc, registry)};

    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed).contains("ruleset"))
        << "SPEC §7: the ruleset block is vendor data and must never be a BARE root member: "
        << json;
    ASSERT_TRUE((*parsed).contains("extensions"))
        << "the stamped ruleset block must serialise under the §7 container: " << json;
    ASSERT_TRUE((*parsed)["extensions"].contains("fr.coderoast.ruleset"))
        << "the stamped ruleset block must serialise as extensions[\"fr.coderoast.ruleset\"]: "
        << json;

    auto& ruleset{(*parsed)["extensions"]["fr.coderoast.ruleset"]};
    ASSERT_TRUE(ruleset.contains("semantic_identity")) << json;
    EXPECT_EQ(ruleset["semantic_identity"].get<std::string>(), "a1b2c3d4e5f60718")
        << "the comparability KEY must round-trip verbatim: " << json;

    ASSERT_TRUE(ruleset.contains("packages")) << json;
    auto& packages{ruleset["packages"].get_array()};
    ASSERT_EQ(packages.size(), 2U) << json;
    EXPECT_EQ(packages[0]["name"].get<std::string>(), "github") << json;
    EXPECT_EQ(packages[0]["version"].get<std::string>(), "1.0.0") << json;
    EXPECT_EQ(packages[1]["name"].get<std::string>(), "test_frameworks") << json;
}

// invariant: a legacy producer with ruleset unset emits NO block, and the consumer tolerates it.
TEST(RulesetIdentity, LegacyProducerEmitsNoBlock)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc_with_ruleset(std::nullopt, &registry)};
    EXPECT_FALSE(doc.ruleset.has_value());

    const std::string json{meta::to_json(doc, registry)};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_FALSE((*parsed).contains("ruleset"))
        << "a legacy producer (no composition injected) must emit NO ruleset block: " << json;
    // invariant: the container itself may exist, the engine stamping a transport declaration on
    // every document, so the assertion is on the ruleset KEY and not on the container.
    if ((*parsed).contains("extensions"))
        EXPECT_FALSE((*parsed)["extensions"].contains("fr.coderoast.ruleset"))
            << "a legacy producer must emit NO ruleset block under the §7 container either: "
            << json;
}

// invariant: two documents with DIFFERENT semantic_identity are not comparable and the gate fails
// closed -- re-segment or refuse, never a silent compare.
TEST(RulesetIdentity, ComposeMismatchedSemanticIdentityThrows)
{
    const auto lhs{build_doc_with_ruleset(kRulesetA)};
    const auto rhs{build_doc_with_ruleset(kRulesetB)};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "SRC-II-7: composing across mismatched composed-ruleset identities MUST fail, never "
           "silently merge";
}

TEST(RulesetIdentity, DiffMismatchedSemanticIdentityThrows)
{
    const auto previous{build_doc_with_ruleset(kRulesetA)};
    const auto current{build_doc_with_ruleset(kRulesetB)};
    EXPECT_THROW(meta::diff(previous, current), std::invalid_argument)
        << "SRC-II-7: diffing across mismatched composed-ruleset identities MUST fail (re-segment "
           "or "
           "refuse upstream)";
}

// note: matching identity is comparable and is carried into the compose() output.
TEST(RulesetIdentity, ComposeMatchingCarriesIdentity)
{
    const auto lhs{build_doc_with_ruleset(kRulesetA)};
    const auto rhs{build_doc_with_ruleset(kRulesetA)};
    const auto out{meta::compose(lhs, rhs)};
    ASSERT_TRUE(out.ruleset.has_value()) << "matched identity must carry into the merged document";
    EXPECT_EQ(out.ruleset->semantic_identity, "a1b2c3d4e5f60718");
}

TEST(RulesetIdentity, DiffMatchingSemanticIdentitySucceeds)
{
    const auto previous{build_doc_with_ruleset(kRulesetA)};
    const auto current{build_doc_with_ruleset(kRulesetA)};
    EXPECT_NO_THROW((void)meta::diff(previous, current));
}

// invariant: with one legacy side the operation MAY proceed, but the output OMITS the identifier
// rather than over-claim a contract the merged document only half-covers.
TEST(RulesetIdentity, ComposeAsymmetricProceedsButOmitsIdentity)
{
    const auto stamped{build_doc_with_ruleset(kRulesetA)};
    const auto legacy{build_doc_with_ruleset(std::nullopt)};
    const auto out{meta::compose(stamped, legacy)};
    EXPECT_FALSE(out.ruleset.has_value()) << "an asymmetric composed-ruleset identity must NOT be "
                                             "carried — over-claiming comparability is unsafe";
}

TEST(RulesetIdentity, DiffAgainstLegacyProceeds)
{
    const auto previous{build_doc_with_ruleset(kRulesetA)};
    const auto current{build_doc_with_ruleset(std::nullopt)};
    EXPECT_NO_THROW((void)meta::diff(previous, current))
        << "diff against a legacy (unstamped) document proceeds — the consumer treats it with "
           "caution";
}
