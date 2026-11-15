
// refs: F-SRC-metalog-spec:SPEC.md, ADR-17.D3
// invariant: SPEC 2.4 defines canonicalization_version as naming masking, TOKENIZATION and
// classification, and makes it a normative comparability GATE that MUST refuse unequal values.
// invariant: the LIBRARY default is the canon-owned masking constant, which the composed package
// set cannot move: canon prefixes it into the preimage it hashes, an INPUT, never a function of it.
// invariant: two documents tokenized under different rulesets at the library default therefore
// carry an EQUAL 2.4 token, and the standard's own gate passes them.
// invariant: the fold closes that at the engine seam: insight-eidos stamps canon's
// ComposedSemantics::canonicalization_version() on every MetaLog configuration it builds.
// note: homed at unit grain: all three things the property needs are already in this binary.
// invariant: what this home CANNOT see is the shipping producer's own configuration, which has its
// own pin in insight-eidos reading the token off a document the real pipeline emitted.
// invariant: the library default stays blind (arm 3), because this package cannot know the
// composition the caller tokenized under; arm 4 proves the folded token is what refuses the pair.
// invariant: no RNG, no wall clock, single-threaded, literal input bytes; both arms consume
// BYTE-IDENTICAL input and the composition is the only variable.
#include <gtest/gtest.h>

import insight.metalog.test;
import insight.semantic.github;

#include "../written_or_fail.hpp"

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// invariant: ONE stream, consumed byte-identically by both arms; two of its lines carry github
// workflow command prefixes whose role rows are supplied BY THE PACKAGE.
// note: the role rows are any-dialect, so they fire without the arm declaring one.
constexpr std::array kStream{
    std::string_view{"##[group]Run actions/checkout@v4"},
    std::string_view{
        "2026-05-31T08:00:01Z INFO request method=GET path=/api/users/1000 status=200"},
    std::string_view{"2026-05-31T08:00:02Z INFO cache key=session:1021 hit=true"},
    std::string_view{"##[error]Process completed with exit code 1."},
    std::string_view{"2026-05-31T08:00:03Z INFO request method=POST path=/api/orders status=201"},
};

using Clock = std::chrono::system_clock;

// note: namespace scope so member initializers can name it; the manifest is a literal type.
constexpr std::array kGithubOnly{insight::semantic::github::kManifest};

// invariant: mirror of the production label -- the package list, which gates nothing.
[[nodiscard]] meta::RulesetIdentity ruleset_of(const insight::semantic::ComposedSemantics& composed)
{
    meta::RulesetIdentity ruleset;
    ruleset.packages.reserve(composed.packages().size());
    for (const insight::semantic::ComposedPackage& pkg : composed.packages())
        ruleset.packages.push_back(
            {.name = std::string{pkg.name}, .version = std::string{pkg.version}});
    return ruleset;
}

struct Arm
{
    meta::MetaLogDocument doc;
    meta::TemplateRegistry registry;
    std::string json;
};

// invariant: canonicalization_version is LEFT AT THE LIBRARY DEFAULT unless `fold` is set; with
// `fold` it is stamped exactly as the engine seam stamps it, from canon's token for `composed`.
[[nodiscard]] Arm build_arm(const insight::semantic::ComposedSemantics& composed,
                            bool stamp_ruleset, bool fold = false)
{
    const Clock::time_point window_start{std::chrono::seconds{1'700'000'000}};
    const Clock::time_point window_end{std::chrono::seconds{1'700'000'060}};

    meta::MetaLogConfig config;
    if (stamp_ruleset)
        config.ruleset = ruleset_of(composed);
    if (fold)
        config.canonicalization_version = composed.canonicalization_version();

    meta::MetaLogEngine engine{config};
    engine.open_window(window_start);

    tok::ArenaAllocator arena{std::size_t{1} << 22};
    tok::Tokenizer tokenizer{arena, tok::MaskConfig{}, composed, tok::StreamContext{}};
    for (const std::string_view raw : kStream)
    {
        const auto event{tokenizer.process_line(raw)};
        if (!event)
            throw std::runtime_error("ruleset coverage: line failed to tokenize: " +
                                     std::string{raw});
        engine.ingest_event(*event);
    }

    auto doc{engine.close_window(window_end)};
    meta::TemplateRegistry registry{engine.registry()};
    std::string json{written_or_fail(meta::to_json(doc, registry))};
    return Arm{.doc = std::move(doc), .registry = std::move(registry), .json = std::move(json)};
}

[[nodiscard]] std::string show(const std::optional<std::string>& value)
{
    return value ? ('"' + *value + '"') : std::string{"<absent>"};
}

[[nodiscard]] std::string packages_of(const insight::semantic::ComposedSemantics& composed)
{
    std::string out{"{"};
    for (const insight::semantic::ComposedPackage& pkg : composed.packages())
        out += " " + std::string{pkg.name} + "@" + std::string{pkg.version};
    return out + " }";
}

// invariant: the two REAL compositions -- canon core alone, the degenerate but defined state, and
// canon core plus the github vocabulary package.
// note: held as a fixture because the composition is move-only and the tokenizer views it.
class RulesetCoverageTest : public ::testing::Test
{
  protected:
    insight::semantic::ComposedSemantics core_only{insight::semantic::compose({})};
    insight::semantic::ComposedSemantics with_github{insight::semantic::compose(kGithubOnly)};
};

} // namespace

// invariant: arm 1, instrument integrity -- if the two compositions were the same object, arms 3
// and 4 would ABORT on their own preconditions, attributing the failure to the wrong defect.
TEST_F(RulesetCoverageTest, TheTwoArmsAreGenuinelyDifferentCompositions)
{
    EXPECT_NE(core_only.identity_hex(), with_github.identity_hex())
        << "the two compositions hash the same, so there is no ruleset change for the rest of this "
           "suite to measure. core_only="
        << core_only.identity_hex() << " " << packages_of(core_only)
        << " with_github=" << with_github.identity_hex() << " " << packages_of(with_github);

    EXPECT_EQ(core_only.packages().size(), 0U) << packages_of(core_only);
    ASSERT_EQ(with_github.packages().size(), 1U) << packages_of(with_github);
    EXPECT_EQ(with_github.packages()[0].name, "github") << packages_of(with_github);
}

// invariant: arm 2, anti-vacuity -- without it the file proves only that two labels differ, and a
// gate passing two labels is harmless.
// invariant: what makes the 2.4 hole a PRECISION defect is that the two documents describe the same
// bytes differently, so composing across them mixes two tokenizations.
// invariant: role_cardinality is the named observable, since StructuralRole rows are supplied by
// PACKAGES: the core-only arm observes one role state while the github arm recognises two more.
// note: asserted on the document AND on the bytes: a difference off the wire harms nobody.
TEST_F(RulesetCoverageTest, TheCompositionSwapChangesWhatTheDocumentSays)
{
    const Arm a{build_arm(core_only, /*stamp_ruleset=*/false)};
    const Arm b{build_arm(with_github, /*stamp_ruleset=*/false)};

    ASSERT_TRUE(a.doc.acquisition.has_value());
    ASSERT_TRUE(b.doc.acquisition.has_value());
    EXPECT_GT(b.doc.acquisition->role_cardinality, a.doc.acquisition->role_cardinality)
        << "the github composition observed " << b.doc.acquisition->role_cardinality
        << " distinct structural roles against the core-only composition's "
        << a.doc.acquisition->role_cardinality
        << ". They must differ: `##[group]`/`##[error]` carry StructuralRole rows that ONLY the "
           "github package supplies, so an empty composition cannot recognise them. Equal here "
           "means the two arms tokenized the same stream identically, and every other arm in this "
           "file is measuring nothing.\n"
           "  core-only document:\n"
        << a.json << "\n  github document:\n"
        << b.json;

    // invariant: neither arm stamped the ruleset member, so the ONLY input that varied is the
    // composition and any byte difference here IS the tokenization difference reaching the wire.
    EXPECT_NE(a.json, b.json)
        << "two rulesets produced byte-identical documents over the same stream, with no ruleset "
           "block stamped on either side — the tokenization difference never reached the wire";
}

// invariant: arm 3, the boundary -- a POSITIVE assertion that must STAY green.
// invariant: metalog can never derive a ruleset-aware token, canon shipping no default composition,
// so the fold lands at the injection seam (insight-eidos stamp_producer_contract).
// invariant: what IS this package's to state is that two genuinely different rulesets producing
// genuinely different documents leave the library's 2.4 token untouched.
// note: it reds if someone teaches the library to synthesize a token it cannot inform.
TEST_F(RulesetCoverageTest, TheLibraryDefaultTokenIsBlindToTheComposition)
{
    const Arm a{build_arm(core_only, /*stamp_ruleset=*/true)};
    const Arm b{build_arm(with_github, /*stamp_ruleset=*/true)};

    ASSERT_NE(core_only.identity_hex(), with_github.identity_hex())
        << "the arms carry the same composed identity — ① should have caught this first";

    // invariant: printed UNCONDITIONALLY and not only on failure -- this arm IS the measurement the
    // ruleset-fold ruling rests on, and one that surfaces only on a break is not on the record.
    std::cout << "[ MEASURED ] one stream, two real rulesets:\n"
              << "             canonicalization_version   = "
              << show(a.doc.canonicalization_version) << " (core-only) / "
              << show(b.doc.canonicalization_version) << " (github)\n"
              << "             composed identity          = " << core_only.identity_hex()
              << " (core-only) / " << with_github.identity_hex() << " (github)\n";

    EXPECT_EQ(a.doc.canonicalization_version, b.doc.canonicalization_version)
        << "the library produced two DIFFERENT §2.4 tokens for two different rulesets:\n"
           "    core_only   = "
        << show(a.doc.canonicalization_version)
        << "\n"
           "    with_github = "
        << show(b.doc.canonicalization_version)
        << "\n"
           "  It has no information to do that with: canon ships no default composition, so a "
           "ruleset-aware token can only be computed by the binary that declares the package set "
           "and injected here. If the fold landed inside this package, it is synthesizing a "
           "comparability key from something that is not the composition — which is worse than the "
           "hole it replaces.";

    // invariant: the measurement -- one stream, two real rulesets, one 2.4 token, and the two
    // values are byte-equal because the token is an INPUT to the ruleset identity.
    EXPECT_EQ(a.doc.canonicalization_version,
              std::optional<std::string>{std::string{insight::kCanonicalizationVersion}})
        << "the default token is no longer insight::kCanonicalizationVersion: "
        << show(a.doc.canonicalization_version)
        << ". This assertion pins WHERE the value comes from; if the default moved, the eidos "
           "production pin is reading a different contract than this file describes.";
}

// invariant: arm 4, the harm and its closure -- at the library default the pair whose tokenizations
// differ is ACCEPTED by compose and diff: no member a conformant consumer reads tells them apart.
// invariant: the package label gates nothing, so it cannot refuse the pair either (ADR-17.D3).
// invariant: stamped as the engine seam stamps it, the same pair is REFUSED by the one 2.4 gate.
// note: compose and diff are asserted together, being the two 2.4-gated operations.
TEST_F(RulesetCoverageTest, TheFoldedTokenIsWhatRefusesTheIncomparablePair)
{
    const auto refuses{
        [](const meta::MetaLogDocument& lhs, const meta::MetaLogDocument& rhs, bool as_diff)
        {
            try
            {
                if (as_diff)
                    (void)meta::diff(lhs, rhs);
                else
                    (void)meta::compose(lhs, rhs);
            }
            catch (const std::invalid_argument&)
            {
                return true;
            }
            return false;
        }};

    const Arm blind_a{build_arm(core_only, /*stamp_ruleset=*/true)};
    const Arm blind_b{build_arm(with_github, /*stamp_ruleset=*/true)};
    EXPECT_FALSE(refuses(blind_a.doc, blind_b.doc, /*as_diff=*/false))
        << "compose() refused the library-default pair, so a gate other than 2.4 still bites on "
           "the label — ADR-17.D3 ripped the second gate. canonicalization_version (both arms) = "
        << show(blind_a.doc.canonicalization_version);
    EXPECT_FALSE(refuses(blind_a.doc, blind_b.doc, /*as_diff=*/true))
        << "diff refused the library-default pair — same, a second gate still bites";

    const Arm folded_a{build_arm(core_only, /*stamp_ruleset=*/true, /*fold=*/true)};
    const Arm folded_b{build_arm(with_github, /*stamp_ruleset=*/true, /*fold=*/true)};
    EXPECT_TRUE(refuses(folded_a.doc, folded_b.doc, /*as_diff=*/false))
        << "compose() accepted two folded tokens: " << show(folded_a.doc.canonicalization_version)
        << " / " << show(folded_b.doc.canonicalization_version);
    EXPECT_TRUE(refuses(folded_a.doc, folded_b.doc, /*as_diff=*/true))
        << "diff accepted two folded tokens: " << show(folded_a.doc.canonicalization_version)
        << " / " << show(folded_b.doc.canonicalization_version);
}
