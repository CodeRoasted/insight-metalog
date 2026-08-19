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
// ALL FOUR ARMS ARE GREEN TODAY, AND THAT IS THE HOMING CALL, NOT AN OVERSIGHT. §2.4's bump MUST
// is a claim about a VALUE, and the value is not this package's to produce: canon ships no default
// composition, so only the BINARY that declares a package set can compute a ruleset-aware token,
// and the fold lands at that injection seam. A "the two tokens must differ" arm homed here would be
// a can't-PASS gate — it would keep skipping after the defect was fixed, because nothing it can
// reach ever changes. That arm lives where it can flip, in
// insight-eidos/engine/tests/pipeline/production_processing_identifier_test.cpp. What this file
// owns is the MECHANISM and the HARM: ① and ② establish that the two rulesets are real and produce
// genuinely different documents; ③ pins the boundary (the library's token cannot see the
// composition, and reds if it ever synthesizes one); ④ exhibits the harm — the only thing refusing
// the incomparable pair is a member SPEC §7 tells consumers to ignore. ④'s subject is the `ruleset`
// member, so it dies with that member at the fold; that is the fold's own cascade, not debt.
//
// Determinism: no RNG, no wall clock (fixed epoch 1'700'000'000 s), single-threaded, literal input
// bytes, integer timing only. Both arms consume BYTE-IDENTICAL input; the composition is the only
// variable.

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

// ── ③ THE BOUNDARY — this library's default token cannot see the composition ───────────────────
//
// GREEN today, and it must STAY green: it is a positive boundary assertion, not a pre-registered
// red, and the difference is a homing call worth stating because the obvious placement is wrong.
//
// §2.4's bump MUST is a claim about a VALUE, and the value is not this package's to produce.
// `MetaLogConfig::canonicalization_version` defaults to a canon-owned constant, and canon ships no
// default composition at all — the composed package set exists only in the BINARY that declares it.
// So metalog can never derive a ruleset-aware token, and the fold (`masking version ⊕ composed
// hash`) lands at the injection seam, in the same producer that injects `composed_semantics()`. A
// "the tokens must differ" arm homed here would therefore be a can't-PASS gate: it would go on
// skipping after the defect was fixed, because nothing it can reach ever changes. That arm lives
// where it can flip — insight-eidos/engine/tests/pipeline/production_processing_identifier_test.cpp
// reads the token off a document the real InsightPipeline emitted.
//
// What IS this package's to state is the boundary: two genuinely different rulesets (①) producing
// genuinely different documents (②) leave the library's §2.4 token untouched. That is the mechanism
// of the §2.4 hole, it is permanent here, and it reds if someone ever teaches the library to
// synthesize a token it has no information to synthesize.
TEST_F(RulesetCoverageTest, TheLibraryDefaultTokenIsBlindToTheComposition)
{
    const Arm a{build_arm(core_only, /*stamp_ruleset=*/true)};
    const Arm b{build_arm(with_github, /*stamp_ruleset=*/true)};

    ASSERT_TRUE(a.doc.ruleset.has_value());
    ASSERT_TRUE(b.doc.ruleset.has_value());
    ASSERT_NE(a.doc.ruleset->semantic_identity, b.doc.ruleset->semantic_identity)
        << "the arms carry the same composed identity — ① should have caught this first";

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

    // THE MEASUREMENT the ruleset-fold ruling rests on, recorded where the mechanism is: one
    // stream, two real rulesets, one §2.4 token. Not a tolerance and not a heuristic — the two
    // values are byte-equal because the token is the canon masking constant, which canon PREFIXES
    // INTO the preimage it hashes into semantic_identity. It is an input to the ruleset identity
    // and can never be a function of it, so no amount of care on this side closes the gap.
    EXPECT_EQ(a.doc.canonicalization_version,
              std::optional<std::string>{std::string{insight::kCanonicalizationVersion}})
        << "the default token is no longer insight::kCanonicalizationVersion: "
        << show(a.doc.canonicalization_version)
        << ". This assertion pins WHERE the value comes from; if the default moved, the eidos "
           "production pin is reading a different contract than this file describes.";
}

// ── ④ THE HARM — the only refusal rides a member the standard tells consumers to ignore ────────
//
// GREEN today. This arm's SUBJECT is the `ruleset` member, so it dies with that member when the
// fold lands — and that is the correct cascade, not debt: once the identity rides
// `canonicalization_version`, "strip the non-standard member" has no referent and the property
// collapses into the eidos pin. It is live and load-bearing until then, because it is the only
// place the actual harm is exhibited rather than argued.
//
// SPEC §7 orders consumers to ignore what they do not know. A second implementer holding only the
// specification has no `ruleset` to check and no reason to look for one, so stripping it is not a
// contrivance — it is what a conformant foreign consumer's view of these two documents IS. Under
// that view the pair, whose tokenizations genuinely differ (②), is ACCEPTED.
//
// compose() and diff are asserted together: they are the two §2.4-gated operations, and a hole open
// on one of them is open.
TEST_F(RulesetCoverageTest, OnlyANonStandardMemberRefusesTheIncomparablePair)
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

    // ORACLE INTEGRITY: the refusal must exist somewhere, or "the standard-only view does not
    // refuse" is measuring a gate that never bites at all rather than one a consumer cannot reach.
    ASSERT_TRUE(refuses(a.doc, b.doc, /*as_diff=*/false))
        << "our own compose() accepted two different composed rulesets — the ruleset gate itself "
           "is broken, which is a different defect from the one this arm is about";
    ASSERT_TRUE(refuses(a.doc, b.doc, /*as_diff=*/true))
        << "our own diff accepted two different composed rulesets";

    meta::MetaLogDocument standard_a{a.doc};
    meta::MetaLogDocument standard_b{b.doc};
    standard_a.ruleset.reset(); // §7: "Consumers MUST ignore unknown extensions."
    standard_b.ruleset.reset();

    EXPECT_FALSE(refuses(standard_a, standard_b, /*as_diff=*/false))
        << "compose() refused the pair with `ruleset` stripped, so the refusal rides a STANDARD "
           "member and this file's whole argument is stale — re-derive it before acting on it. "
           "canonicalization_version (both arms) = "
        << show(standard_a.canonicalization_version);
    EXPECT_FALSE(refuses(standard_a, standard_b, /*as_diff=*/true))
        << "diff refused the pair with `ruleset` stripped — same, re-derive before acting";
}
// NOLINTEND
