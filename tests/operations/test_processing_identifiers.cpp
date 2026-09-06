
// refs: F-SRC-metalog-spec:SPEC.md
// invariant: a document carries two opaque processing identifiers: the masking, tokenization and
// classification rules, and the retention profile with its sizes, caps and arithmetic.
// invariant: they arm a comparability gate on compose() and diff(): when both inputs carry an
// identifier the values MUST be equal, and otherwise the operation MUST fail.
#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

namespace
{
    [[nodiscard]] meta::MetaLogDocument
    build_doc_with_identifiers(const std::optional<std::string>& canon,
                               const std::optional<std::string>& retention,
                               meta::TemplateRegistry* out_registry = nullptr)
    {
        meta::MetaLogConfig cfg{.top_k_size = 8};
        cfg.canonicalization_version = canon;
        cfg.retention_profile = retention;
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

TEST(ProcessingIdentifiers, StampedFromConfigOnDocument)
{
    const auto doc{build_doc_with_identifiers("canon-1", "retention-A")};
    EXPECT_EQ(doc.canonicalization_version, "canon-1");
    EXPECT_EQ(doc.retention_profile, "retention-A");
}

TEST(ProcessingIdentifiers, ComposeCarriesWhenBothMatch)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto out{meta::compose(lhs, rhs)};
    EXPECT_EQ(out.canonicalization_version, "canon-1");
    EXPECT_EQ(out.retention_profile, "retention-A");
}

// invariant: when an input OMITS an identifier the operation may proceed, the gate biting only on
// two present-and-unequal values.
// note: the output omits the asymmetric identifier, since synthesizing one would over-claim.
TEST(ProcessingIdentifiers, ComposeAsymmetricProceedsButOmitsIdentifier)
{
    const auto lhs{build_doc_with_identifiers("canon-1", std::nullopt)};
    const auto rhs{build_doc_with_identifiers(std::nullopt, std::nullopt)};
    const auto out{meta::compose(lhs, rhs)};
    EXPECT_FALSE(out.canonicalization_version.has_value())
        << "asymmetric identifier must NOT be carried — over-claiming a contract is unsafe";
    EXPECT_FALSE(out.retention_profile.has_value());
}

TEST(ProcessingIdentifiers, ComposeMismatchedCanonicalizationVersionThrows)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-2", "retention-A")};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "comparability gate: composing documents whose canonicalization versions are present "
           "and unequal MUST throw — the two windows were not canonicalized alike";
}

TEST(ProcessingIdentifiers, ComposeMismatchedRetentionProfileThrows)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-1", "retention-B")};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "comparability gate: composing documents whose retention profiles are present and "
           "unequal MUST throw — the two windows kept different amounts of evidence";
}

TEST(ProcessingIdentifiers, DiffMismatchedCanonicalizationVersionThrows)
{
    const auto previous{build_doc_with_identifiers("canon-1", std::nullopt)};
    const auto current{build_doc_with_identifiers("canon-2", std::nullopt)};
    EXPECT_THROW(meta::diff(previous, current), std::invalid_argument)
        << "the comparability gate binds diff as well as compose: differencing across "
           "mismatched canonicalization versions MUST throw, never report a false change";
}

TEST(ProcessingIdentifiers, DiffMatchingIdentifiersSucceeds)
{
    const auto previous{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto current{build_doc_with_identifiers("canon-1", "retention-A")};
    EXPECT_NO_THROW((void)meta::diff(previous, current));
}

TEST(ProcessingIdentifiers, SerialisesAtDocumentRoot)
{
    meta::TemplateRegistry registry;
    const auto doc{build_doc_with_identifiers("canon-1", "retention-A", &registry)};
    const std::string json{meta::to_json(doc, registry)};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_TRUE((*parsed).contains("canonicalization_version")) << json;
    EXPECT_TRUE((*parsed).contains("retention_profile")) << json;
}

} // namespace
