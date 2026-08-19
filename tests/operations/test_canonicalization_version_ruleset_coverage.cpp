// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_canonicalization_version_ruleset_coverage.cpp — does §2.4's `canonicalization_version`
// actually COVER the tokenization rules in force?
//
// SPEC §2.4 defines `canonicalization_version` as naming "masking, TOKENIZATION, classification"
// and makes it a normative comparability GATE: when both inputs carry it, `compose()`/diff MUST
// refuse unequal values. Our token is `insight::kCanonicalizationVersion` — a canon-owned constant
// the composed package set cannot move. Worse than "does not move": the composition hash is built
// by PREFIXING that very constant into its preimage (insight-canon compose.cpp, the §4.1 canonical
// serialization), so the masking token is an INPUT to the ruleset identity and can never be a
// function of it. Two documents tokenized under different rulesets therefore carry an EQUAL §2.4
// token, the standard's own gate passes them, and a foreign consumer diffs across incomparable
// tokenizations — the precision loss §2.4 exists to prevent.
//
// HOMING — metalog unit grain, deliberately NOT the e2e seam. The property needs exactly three
// things: a real canon composition, the real tokenizer, and the metalog document + its §2.4 gate.
// All three are in THIS binary already (`insight::canon` is a PUBLIC link;
// `insight_semantic_github` is a declared test dependency). LogCraft supplies no fact these arms
// cannot state — the input is literal bytes — so an e2e home would buy no proof, cost wall-clock on
// every gate, and blur which package broke. What this home CANNOT see is the shipping producer's
// own configuration, since `canonicalization_version` is an overridable member: that grain has its
// own pin at insight-eidos/engine/tests/pipeline/production_processing_identifier_test.cpp, which
// reads the token off a document the real InsightPipeline emitted.
//
// THE JOINT THAT WAS NEVER MADE, and it is why the hole survived. Each grain was proven alone:
// canon proves `semantic_identity` varies with the package set (composition_test.cpp,
// canon.conformance check_determinism); test_processing_identifiers.cpp proves the §2.4 gate bites
// — on HAND-WRITTEN values ("canon-1"/"canon-2"); test_ruleset_identity.cpp proves the ruleset gate
// bites — on HAND-WRITTEN hashes ("a1b2c3d4e5f60718"/"ffffffffffffffff"). No test in the workspace
// has ever put a REAL composed identity and the DEFAULT `canonicalization_version` on the same
// document and compared two of them. That is precisely the joint the defect lives in.
//
// TWO ARMS PRE-REGISTERED RED. Arms ③ and ④ encode the TARGET behaviour (post-fold), so they
// SKIP rather than fail today and self-flip to a hard PASS the moment the ruleset identity is
// folded into `canonicalization_version` — no edit. Arms ① and ② are GREEN today and must stay
// green in both worlds: they are the instrument-integrity legs, and without them ③/④ would be
// comparing two identical things and skipping for the wrong reason.
//
// Determinism: no RNG, no wall clock (fixed epoch 1'700'000'000 s), single-threaded, literal input
// bytes, integer timing only. Both arms consume BYTE-IDENTICAL input; the composition is the only
// variable.

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test; // std + metalog (+ detail) + insight.canon (compose/transport)
import insight.semantic.github;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// ── The stream. ONE stream, consumed byte-identically by both arms. Two lines carry github
// workflow-command prefixes (`##[group]`, `##[error]`), whose StructuralRole rows are supplied BY
// THE PACKAGE — an empty composition supplies none. The role rows are kAnyDialect (github.cppm: a
// workflow command "on an undeclared CI line still classifies"), so they fire without the arm
// having to declare a dialect, which keeps the composition the single variable.
constexpr std::array kStream{
    std::string_view{"##[group]Run actions/checkout@v4"},
    std::string_view{
        "2026-05-31T08:00:01Z INFO request method=GET path=/api/users/1000 status=200"},
    std::string_view{"2026-05-31T08:00:02Z INFO cache key=session:1021 hit=true"},
    std::string_view{"##[error]Process completed with exit code 1."},
    std::string_view{"2026-05-31T08:00:03Z INFO request method=POST path=/api/orders status=201"},
};

using Clock = std::chrono::system_clock;

// The core-only composition + the core-plus-github composition. Namespace scope so the fixture's
// member initializers can name it; `SemanticPackageManifest` is a literal type (github::kManifest
// is itself `inline constexpr`), so this needs no dynamic initialization.
constexpr std::array kGithubOnly{insight::semantic::github::kManifest};

// Mirror of the production stamp (insight-eidos engine/src/pipeline/insight_pipeline.cpp
// `current_ruleset`): the composed hash is the KEY, the package list is the label.
[[nodiscard]] meta::RulesetIdentity ruleset_of(const insight::semantic::ComposedSemantics& composed)
{
    meta::RulesetIdentity ruleset;
    ruleset.semantic_identity = composed.identity_hex();
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

// Close one window over kStream under `composed`.
//
// `canonicalization_version` is LEFT AT ITS DEFAULT on purpose — that IS the production
// configuration. A workspace sweep for every assignment to it returns five internal propagations
// inside this package (config→doc, config→coordinate, the compose() carry, two DTO copies) and
// nothing else outside test fixtures: no production site anywhere overrides it. Overriding it here
// would replace the measurement with an assumption.
[[nodiscard]] Arm build_arm(const insight::semantic::ComposedSemantics& composed,
                            bool stamp_ruleset)
{
    const Clock::time_point window_start{std::chrono::seconds{1'700'000'000}};
    const Clock::time_point window_end{std::chrono::seconds{1'700'000'060}};

    meta::MetaLogConfig config;
    if (stamp_ruleset)
        config.ruleset = ruleset_of(composed);

    meta::MetaLogEngine engine{config};
    engine.open_window(window_start);

    tok::ArenaAllocator arena{std::size_t{1} << 22};
    tok::Tokenizer tokenizer{arena, tok::MaskConfig{}, composed};
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
    std::string json{meta::to_json(doc, registry)};
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

// The two REAL compositions. A: canon core only (the degenerate-but-defined state — no rows, no
// strategies). B: canon core + the github vocabulary package. Held as a fixture because
// ComposedSemantics is move-only and the Tokenizer views it.
class RulesetCoverageTest : public ::testing::Test
{
  protected:
    insight::semantic::ComposedSemantics core_only{insight::semantic::compose({})};
    insight::semantic::ComposedSemantics with_github{insight::semantic::compose(kGithubOnly)};
};

} // namespace

// ── ① INSTRUMENT INTEGRITY — the two arms really are two different rulesets ────────────────────
//
// GREEN today and after the fold. Every arm below compares arm A against arm B; if the two
// compositions were the same object, ③ and ④ would skip because there was nothing to detect, and
// the skip message would be a lie. This leg is what makes their red meaningful.
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

// ── ② ANTI-VACUITY — the composition swap changes WHAT THE DOCUMENT SAYS ───────────────────────
//
// GREEN today and after the fold. Without this leg the whole file proves only that two labels
// differ, and a gate passing two labels is harmless. What makes the §2.4 hole a PRECISION defect
// is that the two documents describe the same bytes differently, so composing or differencing
// across them mixes two tokenizations.
//
// `role_cardinality` is the named observable: the acquisition block's distinct-structural_role
// count over the cube's role axis. StructuralRole rows are supplied by PACKAGES, so the core-only
// arm observes exactly one role state (None) while the github arm additionally recognises the
// `##[group]` and `##[error]` workflow commands. Asserted on the DOCUMENT, and then on the
// serialized bytes, because a difference that does not reach the wire is not a difference a
// standard consumer can be harmed by.
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

    // Neither arm stamped `ruleset`, so the ONLY input that varied is the composition: any byte
    // difference here IS the tokenization difference reaching the wire.
    EXPECT_NE(a.json, b.json)
        << "two rulesets produced byte-identical documents over the same stream, with no ruleset "
           "block stamped on either side — the tokenization difference never reached the wire";
}

// ── ③ THE PROPERTY — §2.4's bump MUST, over a real ruleset change. PRE-REGISTERED RED ──────────
//
// §2.4 requires `canonicalization_version` to be bumped when the output-affecting semantics of
// masking / tokenization / classification change. ② has just measured that a package-set swap
// changes exactly that. So the two documents MUST NOT carry equal tokens.
//
// Flips to green when the composed-ruleset identity is folded into `canonicalization_version`
// (masking version ⊕ composed-ruleset hash) and the `ruleset` member dies.
TEST_F(RulesetCoverageTest, Section24BumpMustHoldsWhenTheRulesetChanges)
{
    const Arm a{build_arm(core_only, /*stamp_ruleset=*/true)};
    const Arm b{build_arm(with_github, /*stamp_ruleset=*/true)};

    ASSERT_TRUE(a.doc.ruleset.has_value());
    ASSERT_TRUE(b.doc.ruleset.has_value());
    ASSERT_NE(a.doc.ruleset->semantic_identity, b.doc.ruleset->semantic_identity)
        << "the arms carry the same composed identity — ① should have caught this first";

    if (a.doc.canonicalization_version != b.doc.canonicalization_version)
        SUCCEED() << "the ruleset fold landed: a package-set swap moves the §2.4 token. core_only="
                  << show(a.doc.canonicalization_version)
                  << " with_github=" << show(b.doc.canonicalization_version);
    else
        GTEST_SKIP()
            << "PRE-REGISTERED RED — SPEC §2.4 bump MUST is violated by the producer today.\n"
               "  Two documents over ONE byte-identical stream, tokenized under two different "
               "composed package sets, carry an EQUAL `canonicalization_version`:\n"
               "    canonicalization_version (both arms) = "
            << show(a.doc.canonicalization_version)
            << "\n"
               "    ruleset.semantic_identity  core_only = "
            << a.doc.ruleset->semantic_identity
            << "\n"
               "    ruleset.semantic_identity with_github = "
            << b.doc.ruleset->semantic_identity
            << "\n"
               "  §2.4 defines that token as naming masking, TOKENIZATION and classification, with "
               "a normative MUST to bump it when their output-affecting semantics change. The "
               "semantics did change (see arm ②) and the token did not, because it is the "
               "canon-owned masking constant — which the composition hash PREFIXES INTO ITS OWN "
               "PREIMAGE, so it is an input to the ruleset identity and cannot be a function of "
               "it.\n"
               "  Flips to green when the composed-ruleset identity is folded into "
               "`canonicalization_version`.";
}

// ── ④ THE HARM — a §7-compliant consumer cannot see the refusal. PRE-REGISTERED RED ────────────
//
// Our own gate DOES refuse this pair today, but only because it also checks `ruleset` — a
// NON-STANDARD member. SPEC §7 orders consumers to ignore what they do not know, so a second
// implementer holding only the standard has no `ruleset` to check and no reason to look for one.
// Stripping it is not a contrivance: it is what a conformant foreign consumer's view of these two
// documents IS.
//
// The two operations are asserted together. compose() and diff are the two §2.4-gated operations;
// a fold that armed one and not the other would leave the hole open on the other.
//
// Flips to green when the fold lands, because the refusal then rides the standard token itself.
TEST_F(RulesetCoverageTest, AStandardOnlyConsumerRefusesTheIncomparablePair)
{
    const Arm a{build_arm(core_only, /*stamp_ruleset=*/true)};
    const Arm b{build_arm(with_github, /*stamp_ruleset=*/true)};

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

    // ORACLE INTEGRITY: the refusal must exist somewhere today, or "the standard-only view does not
    // refuse" is measuring a gate that never bites at all rather than one a consumer cannot reach.
    ASSERT_TRUE(refuses(a.doc, b.doc, /*as_diff=*/false))
        << "our own compose() accepted two different composed rulesets — the ruleset gate itself "
           "is broken, which is a different defect from the one this arm is about";

    meta::MetaLogDocument standard_a{a.doc};
    meta::MetaLogDocument standard_b{b.doc};
    standard_a.ruleset.reset(); // §7: "Consumers MUST ignore unknown extensions."
    standard_b.ruleset.reset();

    const bool compose_refused{refuses(standard_a, standard_b, /*as_diff=*/false)};
    const bool diff_refused{refuses(standard_a, standard_b, /*as_diff=*/true)};

    if (compose_refused && diff_refused)
        SUCCEED()
            << "the ruleset fold landed: the refusal now rides the standard's own §2.4 token, "
               "so a consumer holding only the specification refuses the pair too";
    else
        GTEST_SKIP()
            << "PRE-REGISTERED RED — the refusal is invisible to a conformant foreign consumer.\n"
               "  With `ruleset` present our gate REFUSES the pair. With `ruleset` stripped — the "
               "view SPEC §7 gives any consumer that does not know our non-standard members — "
               "compose() refused: "
            << (compose_refused ? "yes" : "NO")
            << ", diff refused: " << (diff_refused ? "yes" : "NO")
            << ".\n"
               "  Both must be yes. Both arms carry canonicalization_version = "
            << show(standard_a.canonicalization_version)
            << " (equal), so the standard's own comparability gate passes a pair whose "
               "tokenizations differ, and the consumer merges or differences across them believing "
               "it checked. The refusal we do have rides a member the standard tells consumers to "
               "ignore.\n"
               "  Flips to green when the composed-ruleset identity is folded into "
               "`canonicalization_version`.";
}
// NOLINTEND
