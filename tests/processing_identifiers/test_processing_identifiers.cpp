// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// §2.4 processing identifiers + comparability gate across compose()/diff().

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

// ── §2.4 processing identifiers + comparability gate ─────────────────────────

namespace
{
[[nodiscard]] meta::MetaLogDocument
build_doc_with_identifiers(const std::optional<std::string>& canon,
                           const std::optional<std::string>& retention)
{
    meta::MetaLogConfig cfg{.top_k_size = 8};
    cfg.canonicalization_version = canon;
    cfg.retention_profile = retention;
    meta::MetaLogEngine engine{cfg};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("alpha"));
    return engine.close_window(start + std::chrono::seconds(60));
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

// §2.4: "When an input omits an identifier, the operation MAY proceed but the
// consumer SHOULD treat the result with caution." We proceed but OMIT the
// asymmetric identifier on the output rather than over-claim a contract.
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
        << "§2.4 gate: canon mismatch MUST fail";
}

TEST(ProcessingIdentifiers, ComposeMismatchedRetentionProfileThrows)
{
    const auto lhs{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto rhs{build_doc_with_identifiers("canon-1", "retention-B")};
    EXPECT_THROW(meta::compose(lhs, rhs), std::invalid_argument)
        << "§2.4 gate: retention profile mismatch MUST fail";
}

TEST(ProcessingIdentifiers, DiffMismatchedCanonicalizationVersionThrows)
{
    const auto previous{build_doc_with_identifiers("canon-1", std::nullopt)};
    const auto current{build_doc_with_identifiers("canon-2", std::nullopt)};
    EXPECT_THROW(meta::diff(previous, current), std::invalid_argument)
        << "§2.4 gate at §13: diff across mismatched canon MUST fail";
}

TEST(ProcessingIdentifiers, DiffMatchingIdentifiersSucceeds)
{
    const auto previous{build_doc_with_identifiers("canon-1", "retention-A")};
    const auto current{build_doc_with_identifiers("canon-1", "retention-A")};
    EXPECT_NO_THROW((void)meta::diff(previous, current));
}

TEST(ProcessingIdentifiers, SerialisesAtDocumentRoot)
{
    const auto doc{build_doc_with_identifiers("canon-1", "retention-A")};
    const std::string json{meta::to_json(doc)};
    const auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    EXPECT_TRUE((*parsed).contains("canonicalization_version")) << json;
    EXPECT_TRUE((*parsed).contains("retention_profile")) << json;
}

} // namespace

// NOLINTEND
