// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_compose_algebra.cpp — SPEC §12.2's three algebraic clauses over `compose()`, which had NO
// test anywhere in the reference implementation until this file (`rg associativ tests/` returned
// zero; DN-56.O3). §12.2 states three properties and they carry three different modal strengths,
// so they are separate arms rather than one "algebra" test:
//
//   * Commutativity — **MUST**, over all required fields.  `compose(A,B)` vs `compose(B,A)`.
//   * Identity      — **MUST**.                            `compose(A, ZERO)` vs `A`.
//   * Associativity — **SHOULD** (best-effort).            `(A∘B)∘C` vs `A∘(B∘C)`.
//
// WHERE THIS FILE STANDS NOW, because two of its arms were written to be RED and are not any more.
// The composed-document cap (DN-56.D2: every cap of `C` is the MINIMUM over the caps the inputs
// actually declared) has LANDED. Commutativity and identity were shipped violations of a §12.2
// MUST and are now GREEN. Associativity was green and the cap is what broke it — DN-56.D3 prices
// that in writing and rules that it yields, because §12.2 scopes it as a SHOULD over required
// fields while the bounded document is the format's headline property.
//
// SO THE ASSOCIATIVITY ARMS ARE NO LONGER ASSERTIONS THAT IT HOLDS. They assert the exact,
// deterministic scope-dependence DN-56.D3 rules, from BOTH sides — the divergence when the cap
// binds, and the equality when it does not. Every expected magnitude below is DERIVED from the
// frozen band ladder and written out; none is transcribed from a run. If the implementation
// diverges differently than the design predicts, these arms FAIL — a boundary asserted from one
// side only is a golden re-pinned for green.
//
// HOMING (Kleio, 2026-08-24). Unit, `insight-metalog/tests/operations/`. `compose()` is a pure
// function of two `MetaLogDocument` values; every one of these three properties is a statement
// about that function's own domain and codomain and crosses no package seam. An integration home
// would cost wall-clock on every gate and blur which package broke, and would buy no proof the
// call itself does not already give.
//
// THE ORACLE IS THE ALGEBRA, NEVER A SECOND CALL INTO `compose()`. Where a clause is inherently
// relational (commutativity and associativity ARE relations between two calls of the SUT), the arm
// also pins the exact magnitudes each side produces — band constant × rarity constant, written out
// — so a relation that holds because both sides collapsed to the same degenerate value cannot pass.
//
// Determinism: no RNG, no threads, no wall clock. Every window uses a literal epoch-offset
// time_point; the hand-built documents carry literal ISO strings. Single-threaded by construction.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT1{std::chrono::seconds{1700000060}};

// ── Shared readers ────────────────────────────────────────────────────────────

// The emitted order matters for every clause here (§12.1 truncates a SORTED array), so the
// signature is a sequence, never a set.
[[nodiscard]] std::vector<std::pair<insight::TemplateId, std::uint64_t>>
top_k_signature(const meta::MetaLogDocument& doc)
{
    std::vector<std::pair<insight::TemplateId, std::uint64_t>> out;
    out.reserve(doc.stats.top_k.size());
    for (const auto& entry : doc.stats.top_k)
        out.emplace_back(entry.template_id, entry.count);
    return out;
}

[[nodiscard]] std::string render_top_k(const meta::MetaLogDocument& doc)
{
    std::string out{"top_k_size=" + std::to_string(doc.stats.top_k_size) + " top_k[" +
                    std::to_string(doc.stats.top_k.size()) + "]:"};
    for (const auto& entry : doc.stats.top_k)
        out += "\n      " + insight::render(entry.template_id) +
               " count=" + std::to_string(entry.count);
    return out;
}

[[nodiscard]] std::string render_reservoir(const meta::MetaLogDocument& doc)
{
    std::string out{"reservoir_size="};
    out += doc.stats.reservoir_size ? std::to_string(*doc.stats.reservoir_size) : "<absent>";
    out += " reservoir[" + std::to_string(doc.stats.reservoir.size()) + "]:";
    for (const auto& entry : doc.stats.reservoir)
        out += "\n      " + insight::render(entry.template_id) +
               " count=" + std::to_string(entry.count) +
               " salience=" + std::to_string(entry.salience) +
               " surprise=" + std::to_string(entry.structural_surprise);
    return out;
}

// The reservoir as an ORDERED sequence of everything §12.1 says composition derives for it:
// membership, merged count, and the re-derived salience that decides the rank.
[[nodiscard]] std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>>
reservoir_signature(const meta::MetaLogDocument& doc)
{
    std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>> out;
    out.reserve(doc.stats.reservoir.size());
    for (const auto& entry : doc.stats.reservoir)
        out.emplace_back(entry.template_id, entry.count, entry.salience);
    return out;
}

[[nodiscard]] const meta::ReservoirEntry* reservoir_find(const meta::MetaLogDocument& doc,
                                                         insight::TemplateId id)
{
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_id == id)
            return &entry;
    return nullptr;
}

// ── §12.2 COMMUTATIVITY (MUST) ────────────────────────────────────────────────
//
// "compose(A, B) and compose(B, A) MUST agree on all required fields."
//
// `stats.top_k_size` and `behavior.top_ngrams_size` are both REQUIRED by the published schema
// (`schema/metalog.v0.schema.json`: stats.required contains `top_k_size`, behavior.required
// contains `top_ngrams_size`). `compose()` used to take BOTH from `lhs`, and picking one side is
// inherently non-commutative: with mismatched producers the same PAIR yielded two documents that
// disagreed on a required field AND carried differently-truncated arrays (DN-56.D6, a shipped
// MUST violation this arm was written red to expose).
//
// IT IS GREEN BECAUSE THE COMPOSED CAP IS NOW `min` OVER THE DECLARED CAPS (DN-56.D2), and `min`
// is symmetric — the clause holds by construction rather than by coincidence. Do not "repair" a
// future red here by asserting a side-bias: a red means a cap site went back to picking a side,
// or a new capped block landed without joining the rule.
//
// WHAT THE ARM WOULD BE VACUOUS WITHOUT: the preconditions below. If the two producers declared
// the SAME cap, or if the union were small enough that neither cut bit, both sides would agree by
// construction and the green would say nothing at all.
TEST(ComposeAlgebraTest, CommutativityHoldsOnTheRequiredCapFields)
{
    // Two producers over overlapping-but-different template alphabets, differing ONLY in the two
    // cap knobs. Same event budget on both sides so nothing but the caps can explain a divergence.
    const auto window{
        [](std::string_view prefix, std::size_t top_k, std::size_t reservoir,
           std::size_t top_ngrams)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{
                .top_k_size = top_k,
                .reservoir_size = reservoir,
                .top_ngrams_size = top_ngrams,
                .emit_stability = false,
                .max_param_histograms = 0,
            }};
            engine.open_window(kT0);
            // 12 distinct templates, each at a distinct frequency so the top-K
            // ranking is total and carries no ties.
            for (int t = 0; t < 12; ++t)
                for (int rep = 0; rep <= t; ++rep)
                    engine.ingest_event(make_event(std::string{prefix} + std::to_string(t)));
            return engine.close_window(kT1);
        }};

    const auto wide{window("wide template ", /*top_k=*/8, /*reservoir=*/8, /*top_ngrams=*/8)};
    const auto narrow{window("narrow template ", /*top_k=*/3, /*reservoir=*/2, /*top_ngrams=*/2)};

    // ── Preconditions: the discriminator is live on ALL THREE cap sites. ──
    ASSERT_NE(wide.stats.top_k_size, narrow.stats.top_k_size)
        << "the two inputs must declare DIFFERENT top_k_size or this arm cannot separate a "
           "commutative composer from an lhs-biased one";
    ASSERT_TRUE(wide.behavior.has_value() && narrow.behavior.has_value());
    ASSERT_NE(wide.behavior->top_ngrams_size, narrow.behavior->top_ngrams_size)
        << "same requirement on the second cap";
    ASSERT_TRUE(wide.stats.reservoir_size.has_value() && narrow.stats.reservoir_size.has_value())
        << "both inputs must DECLARE a reservoir cap — an absent one is skipped by the `min`, so "
           "the third site would be commutative for a reason this arm is not testing";
    ASSERT_NE(*wide.stats.reservoir_size, *narrow.stats.reservoir_size)
        << "and they must differ, same requirement as the two above";
    ASSERT_EQ(wide.stats.top_k.size(), 8U) << render_top_k(wide);
    ASSERT_EQ(narrow.stats.top_k.size(), 3U) << render_top_k(narrow);

    const auto ab{meta::compose(wide, narrow)};
    const auto ba{meta::compose(narrow, wide)};

    // The union must EXCEED the wider cap, or the wide cut never bites and both sides could agree
    // for a reason that has nothing to do with commutativity.
    ASSERT_GT(ab.stats.unique_templates, wide.stats.top_k_size)
        << "the merged union (" << ab.stats.unique_templates << ") must exceed the wider cap ("
        << wide.stats.top_k_size << ") so BOTH truncations are live";

    // ── Controls: what commutativity already holds today. These must stay green, and they are
    // what proves the reds below are about the CAP and not about a merge that differs wholesale.
    EXPECT_EQ(ab.window.lines_observed, ba.window.lines_observed)
        << "lines_observed is a sum — commutative by construction";
    EXPECT_EQ(ab.stats.unique_templates, ba.stats.unique_templates)
        << "the union's cardinality does not depend on argument order";

    // ── §12.2's MUST, on the first required cap. ──
    EXPECT_EQ(ab.stats.top_k_size, ba.stats.top_k_size)
        << "SPEC §12.2 commutativity is a MUST over REQUIRED fields, and `stats.top_k_size` is "
           "required (schema stats.required). compose(A,B) and compose(B,A) declared different "
           "caps for the same pair — the composed cap is taken from `lhs` (DN-56.D6).\n"
        << "    compose(wide, narrow): " << render_top_k(ab) << "\n"
        << "    compose(narrow, wide): " << render_top_k(ba);
    EXPECT_EQ(top_k_signature(ab), top_k_signature(ba))
        << "and the declared cap is the array's admission bound, so the two documents also carry "
           "different top_k CONTENT for the same input pair.\n"
        << "    compose(wide, narrow): " << render_top_k(ab) << "\n"
        << "    compose(narrow, wide): " << render_top_k(ba);

    // ── §12.2's MUST, on the second required cap. ──
    ASSERT_TRUE(ab.behavior.has_value() && ba.behavior.has_value())
        << "both compositions must carry a behavior block for the n-gram cap arm to mean anything";
    EXPECT_EQ(ab.behavior->top_ngrams_size, ba.behavior->top_ngrams_size)
        << "`behavior.top_ngrams_size` is required too (schema behavior.required), and "
           "merge_behavior takes it from `lhs` (DN-56.D6). Got "
        << ab.behavior->top_ngrams_size << " vs " << ba.behavior->top_ngrams_size
        << " (top_ngrams arrays: " << ab.behavior->top_ngrams.size() << " vs "
        << ba.behavior->top_ngrams.size() << " entries)";
    EXPECT_EQ(ab.behavior->top_ngrams.size(), ba.behavior->top_ngrams.size())
        << "the n-gram array is re-truncated to that cap, so it diverges with it";

    // ── The third cap site, and its modal strength is stated rather than borrowed. ──
    // `stats.reservoir_size` is OPTIONAL, so §12.2's MUST — which is scoped to required fields —
    // does not reach it. It is asserted here anyway because the field is a §8-clause-4 CLAIM about
    // how large the array may be, and a claim whose value depends on which argument was written
    // first is a defect whatever the clause's modal strength. It is also the site whose absence
    // handling identity pins, so having both arms hold it is deliberate, not duplication.
    EXPECT_EQ(ab.stats.reservoir_size, ba.stats.reservoir_size)
        << "the composed reservoir cap must not depend on argument order.\n"
        << "    compose(wide, narrow): " << render_reservoir(ab) << "\n"
        << "    compose(narrow, wide): " << render_reservoir(ba);
    EXPECT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{2U})
        << "and it is the MINIMUM over the declared caps (8 and 2), not either input's own — a "
           "merge is never finer than its coarsest member. Got "
        << (ab.stats.reservoir_size ? std::to_string(*ab.stats.reservoir_size)
                                    : std::string{"<absent>"});

    // The composed caps are the min of the two, never lhs's — pinned as VALUES so a composer that
    // became commutative by taking the MAXIMUM (equally symmetric, and wrong: it would admit an
    // array wider than the coarsest input's own bound) reds here rather than passing above.
    EXPECT_EQ(ab.stats.top_k_size, narrow.stats.top_k_size)
        << "min(8, 3) = 3; got " << ab.stats.top_k_size;
    EXPECT_EQ(ab.behavior->top_ngrams_size, narrow.behavior->top_ngrams_size)
        << "min(8, 2) = 2; got " << ab.behavior->top_ngrams_size;
}

// ── §12.2 IDENTITY (MUST) ─────────────────────────────────────────────────────
//
// "compose(A, ZERO) MUST equal A, where ZERO is a MetaLog with lines_observed = 0 and empty
// stats."
//
// THE SCOPE, STATED RATHER THAN DISCOVERED. Literal whole-document equality is refuted by §12.1
// itself, which mandates that `C.provenance` gain an entry per input — so a conformant composer
// CANNOT return a document byte-equal to `A`. This arm therefore asserts identity over every
// required field plus every optional field `A` itself DECLARES, and excludes `provenance` and
// `coordinate` (the two blocks §12.1 orders the composer to construct). Those exclusions are
// named here so a future reader does not read this arm as covering them.
//
// AND THE FIXTURE REMOVES THE OTHER ESCAPE. §12.3 makes composition lossy whenever an input had a
// non-empty tail, so an identity red over a tailed document would be §12.3 working, not a defect.
// The fixture below is built so `A` has an EMPTY tail (every template is in top_k or the
// reservoir), which is asserted before anything else. With no tail there is no §12.3 loss, and
// every remaining difference is the composer's.
//
// THE LOAD-BEARING FIELD IS `stats.reservoir_size`, and it is why this arm must exist BEFORE the
// composed-caps fix rather than after. `ZERO` declares no cap at all (asserted below — the engine
// declares `reservoir_size` at the admission site, which an event-free window never reaches). A
// `min`-over-inputs fallback that treats "absent" as a value, or as a reason to omit, silently
// drops the cap `A` declared; a fallback that reads `min` over the caps ACTUALLY DECLARED keeps
// it. Identity is the only clause that separates those two implementations, and it separates them
// exactly here.
TEST(ComposeAlgebraTest, IdentityPreservesTheDocumentIncludingItsDeclaredReservoirCap)
{
    const meta::MetaLogConfig cfg{
        .top_k_size = 3,
        .reservoir_size = 8,
        .top_ngrams_size = 0, // no behavior block: this arm is about `stats`
        .emit_stability = false,
        .max_param_histograms = 0,
    };

    meta::MetaLogEngine engine_a{cfg};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 100; ++rep)
    {
        engine_a.ingest_event(make_event("alpha steady event"));
        engine_a.ingest_event(make_event("beta steady event"));
        engine_a.ingest_event(make_event("gamma steady event"));
    }
    engine_a.ingest_event(make_event("connection refused to db", insight::LogLevel::Error));
    const auto doc_a{engine_a.close_window(kT1)};

    // ZERO: same producer, same contract identifiers, same reported window — and no events.
    meta::MetaLogEngine engine_z{cfg};
    engine_z.open_window(kT0);
    const auto zero{engine_z.close_window(kT1)};

    // ── Preconditions on A: the fixture is the one the scope above describes. ──
    ASSERT_EQ(doc_a.window.lines_observed, 301U) << "3 steady × 100 + 1 rare";
    ASSERT_EQ(doc_a.stats.unique_templates, 4U);
    ASSERT_EQ(doc_a.stats.top_k.size(), 3U) << render_top_k(doc_a);
    ASSERT_EQ(doc_a.stats.reservoir.size(), 1U) << render_reservoir(doc_a);
    ASSERT_EQ(doc_a.stats.tail_unique, 0U)
        << "A must have an EMPTY tail, or §12.3 lossiness — not the composer — explains any red "
           "below. tail_count="
        << doc_a.stats.tail_count;
    ASSERT_TRUE(doc_a.stats.reservoir_size.has_value())
        << "A must DECLARE a reservoir cap or the load-bearing assertion is vacuous";
    ASSERT_EQ(*doc_a.stats.reservoir_size, 8U);

    // ── Preconditions on ZERO: it is genuinely a zero, and it declares NO cap. ──
    ASSERT_EQ(zero.window.lines_observed, 0U);
    ASSERT_TRUE(zero.stats.top_k.empty());
    ASSERT_TRUE(zero.stats.reservoir.empty());
    ASSERT_FALSE(zero.stats.reservoir_size.has_value())
        << "the discriminator this arm exists for: ZERO must declare NO reservoir cap. If it "
           "declares one, a `min` over BOTH inputs and a `min` over the DECLARED inputs give the "
           "same answer and this arm can no longer tell them apart. It declared "
        << *zero.stats.reservoir_size;
    ASSERT_EQ(zero.stats.top_k_size, doc_a.stats.top_k_size)
        << "same config on both sides — the top_k cap is deliberately NOT the variable here (that "
           "is the commutativity arm's job)";

    const auto composed{meta::compose(doc_a, zero)};

    // ── Identity over the window and the required stats fields. ──
    EXPECT_EQ(composed.metalog_version, doc_a.metalog_version);
    EXPECT_EQ(composed.window.start_iso, doc_a.window.start_iso);
    EXPECT_EQ(composed.window.end_iso, doc_a.window.end_iso);
    EXPECT_EQ(composed.window.duration_seconds, doc_a.window.duration_seconds);
    EXPECT_EQ(composed.window.lines_observed, doc_a.window.lines_observed);
    EXPECT_EQ(composed.source, doc_a.source);
    EXPECT_EQ(composed.canonicalization_version, doc_a.canonicalization_version);
    EXPECT_EQ(composed.retention_profile, doc_a.retention_profile);
    EXPECT_EQ(composed.stats.unique_templates, doc_a.stats.unique_templates);
    EXPECT_EQ(composed.stats.top_k_size, doc_a.stats.top_k_size);
    EXPECT_EQ(top_k_signature(composed), top_k_signature(doc_a))
        << "compose(A, ZERO) must reproduce A's top_k exactly.\n    A:        "
        << render_top_k(doc_a) << "\n    composed: " << render_top_k(composed);
    EXPECT_EQ(composed.stats.tail_count, doc_a.stats.tail_count);
    EXPECT_EQ(composed.stats.tail_unique, doc_a.stats.tail_unique);

    // ── Identity over the reservoir: membership, merged count, re-derived salience. ──
    EXPECT_EQ(reservoir_signature(composed), reservoir_signature(doc_a))
        << "the re-derivation is over A's own counts and A's own line total, so it must land on "
           "A's own salience.\n    A:        "
        << render_reservoir(doc_a) << "\n    composed: " << render_reservoir(composed);

    // ── THE LOAD-BEARING ASSERTION (DN-56.O3 item 2). ──
    EXPECT_EQ(composed.stats.reservoir_size, doc_a.stats.reservoir_size)
        << "SPEC §12.2 identity is a MUST: compose(A, ZERO) must equal A, and A declared a "
           "reservoir cap of "
        << (doc_a.stats.reservoir_size ? std::to_string(*doc_a.stats.reservoir_size)
                                       : std::string{"<absent>"})
        << ". The composed document declared "
        << (composed.stats.reservoir_size ? std::to_string(*composed.stats.reservoir_size)
                                          : std::string{"<absent>"})
        << ".\n    Composing with an EMPTY document must not cost a document its declared bound: "
           "a §8-clause-4 cap that survives compose is what makes the clause checkable on a "
           "composed document at all.\n    A:        "
        << render_reservoir(doc_a) << "\n    composed: " << render_reservoir(composed);

    // `stats.entropy_bits` is §12.1's own clause with its own root, and it is asserted — including
    // its identity leg — in `ComposedEntropyBitsIsRecomputedFromTheMergedCounts` below, NOT here.
    // The separation is deliberate and it is about diagnosis, not tidiness: this arm owns the
    // composed-cap fix, that one owns the recomputation clause, and a single red must never be
    // ambiguous about which of the two moved.
}

// ── §12.2 ASSOCIATIVITY (SHOULD) — a DECLARED BOUNDARY, asserted from both sides ──────────────
//
// READ THIS BEFORE CHANGING ANY ARM BELOW.
//
// The composed reservoir is NOT associative, by design (DN-56.D2 + DN-56.D3). These arms do not
// characterize whatever the code happens to do: they assert the exact scope-dependence the design
// RULES, and the equality it rules where no cap binds. An implementation that diverges by a
// different amount, or at a different rung, FAILS here — that is the whole difference between a
// declared boundary and a golden re-pinned for green.
//
// WHY IT YIELDS. Top-M selection under a FIXED total order is associative. The reservoir's order
// is not fixed: §12.1 mandates that salience be re-derived over the merged counts precisely
// because rarity shifts on merge, so the ranking KEY moves with the merge scope. With no bound
// nothing is ever dropped, membership is the union of unions, and the final re-derivation runs at
// the same total scope either way — associativity holds, and the arm below proves it does. Under
// an admission bound an entry cut at a low rung folds into `tail_count` and can never re-enter at
// the scale where it would have ranked, so the low rung makes a cut the high rung would not have
// made. §12.2 scopes associativity as a SHOULD and the bounded document is the format's headline
// property; DN-56.D3 rules that this is the trade, and §12.2's carve-out owes the disclosure.
//
// AND IT COSTS A REQUIRED FIELD TOO, which DN-056's prose does not say. `stats.unique_templates`
// is REQUIRED, and it diverges 7 vs 8 in the strict-flip arm below — a cut entry leaves the
// document entirely, so the next rung's union cannot see it. That is asserted, not narrated: the
// disclosure §12.2 owes is wider than "an unwritten property of an optional block".
//
// THE EXACT VALUES ARE THE FROZEN BAND LADDER, WRITTEN OUT — never a second call into
// salience_score(), never a number read off a run. `src/stats/salience.cpp`: severity bands
// kBandOffPath 75, kBandError 80, kBandStrongOffPath 90, kBandFatal 100; rarity kRarityUncommon
// 90 (count·100 < lines, i.e. < 1 %), kRarityRare 100 (count·1000 < lines, i.e. < 0.1 %).
// `salience = severity × rarity`, so a flip on widening needs 0.9·sev_Q < sev_P < sev_Q.

namespace
{
    struct TopKSeed
    {
        std::string_view tmpl;
        std::uint64_t count;
    };
    struct ReservoirSeed
    {
        std::string_view tmpl;
        std::uint64_t count;
        std::uint32_t structural_surprise;
        std::optional<insight::LogLevel> level;
        std::uint32_t salience; // as the ORIGINATING producer scored it, at its own line total
    };

    // Hand-built inputs, on purpose. `compose()`'s domain is two documents, so a document IS its
    // natural input; building them directly is what lets the band pair and the line totals be
    // placed exactly where DN-56.D3's arithmetic puts them, instead of reverse-engineering a hub
    // fan-out that happens to land on a 2.5 % transition probability. Every value seeded here is
    // drawn from the producer's own frozen alphabet (surprise bands 75/90, levels Error/Fatal).
    [[nodiscard]] meta::MetaLogDocument
    make_document(std::string_view start_iso, std::string_view end_iso,
                  std::uint64_t lines_observed, std::size_t top_k_size,
                  std::optional<std::size_t> reservoir_size, std::initializer_list<TopKSeed> top_k,
                  std::initializer_list<ReservoirSeed> reservoir)
    {
        meta::MetaLogDocument doc;
        doc.window.start_iso = start_iso;
        doc.window.end_iso = end_iso;
        doc.window.duration_seconds = 60;
        doc.window.lines_observed = lines_observed;
        doc.canonicalization_version = "canon-compose-algebra";
        doc.retention_profile = "retention-compose-algebra";
        doc.stats.top_k_size = top_k_size;
        doc.stats.reservoir_size = reservoir_size;
        const auto frequency{[lines_observed](std::uint64_t count)
                             {
                                 return lines_observed > 0 ? static_cast<double>(count) /
                                                                 static_cast<double>(lines_observed)
                                                           : 0.0;
                             }};
        for (const auto& seed : top_k)
        {
            meta::TopKEntry entry;
            entry.template_id = insight::template_id_of(seed.tmpl);
            entry.count = seed.count;
            entry.frequency = frequency(seed.count);
            doc.stats.top_k.push_back(std::move(entry));
        }
        for (const auto& seed : reservoir)
        {
            meta::ReservoirEntry entry;
            entry.template_id = insight::template_id_of(seed.tmpl);
            entry.count = seed.count;
            entry.frequency = frequency(seed.count);
            if (seed.level)
                entry.dominant_level = insight::EventLevel::inferred(*seed.level);
            entry.structural_surprise = seed.structural_surprise;
            entry.salience = seed.salience;
            doc.stats.reservoir.push_back(std::move(entry));
        }
        doc.stats.unique_templates = doc.stats.top_k.size() + doc.stats.reservoir.size();
        return doc;
    }

    // The three documents both arms compose, parameterised only by the salient pair.
    // Line totals are the load-bearing numbers and they are chosen so the rarity band of the
    // count-3 entry, and ONLY that band, moves between the two-document rung and the
    // three-document rung:
    //     A∘B   = 2405 lines → 3/2405 = 0.125 %  → kRarityUncommon (90): < 1 %, not < 0.1 %
    //     A∘B∘C = 3605 lines → 3/3605 = 0.083 %  → kRarityRare     (100): < 0.1 %
    // while the count-2 entry is already below 0.1 % at 2/2405 = 0.083 % and stays at 100.
    // `top_k_size = 4` keeps both salient entries below the composed top-K at EVERY rung
    // (the widest rung ranks six fillers above them), so they are always reservoir candidates.
    constexpr std::size_t kTopKSize{4};
    // The cap is the VARIABLE of this section, not a constant of it: the same three documents are
    // composed at a cap that BINDS (1 — one slot for two salient templates) and at one that cannot
    // (8 — more slots than there are candidates). The pair of runs is what asserts DN-56.D3's
    // boundary from both sides instead of merely declaring it.
    constexpr std::size_t kBindingCap{1};
    constexpr std::size_t kSlackCap{8};

    [[nodiscard]] meta::MetaLogDocument make_a(const ReservoirSeed& salient, std::size_t cap)
    {
        return make_document("2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", 1202, kTopKSize, cap,
                             {{"filler a one", 700}, {"filler a two", 500}}, {salient});
    }
    [[nodiscard]] meta::MetaLogDocument make_b(const ReservoirSeed& salient, std::size_t cap)
    {
        return make_document("2026-01-01T00:01:00Z", "2026-01-01T00:02:00Z", 1203, kTopKSize, cap,
                             {{"filler b one", 700}, {"filler b two", 500}}, {salient});
    }
    // C carries no salient template of its own — it is pure merge scope, which is exactly the
    // variable under test. It declares no reservoir cap, which is what the producer does for a
    // window that admitted nothing.
    [[nodiscard]] meta::MetaLogDocument make_c()
    {
        return make_document("2026-01-01T00:02:00Z", "2026-01-01T00:03:00Z", 1200, kTopKSize,
                             std::nullopt, {{"filler c one", 700}, {"filler c two", 500}}, {});
    }

    [[nodiscard]] std::uint32_t salience_at(const meta::MetaLogDocument& doc,
                                            insight::TemplateId id, const char* what)
    {
        const auto* entry{reservoir_find(doc, id)};
        EXPECT_NE(entry, nullptr) << what << " is absent from the composed reservoir; "
                                  << render_reservoir(doc);
        return entry != nullptr ? entry->salience : 0U;
    }

    // The highest-ranked entry's id, as an optional rather than a `front()` on a possibly-empty
    // array: an arm that reads the leader must red on an empty reservoir, never skip past it.
    [[nodiscard]] std::optional<insight::TemplateId>
    leading_reservoir_id(const meta::MetaLogDocument& doc)
    {
        EXPECT_FALSE(doc.stats.reservoir.empty())
            << "the composed reservoir is empty — nothing ranks; " << render_reservoir(doc);
        if (doc.stats.reservoir.empty())
            return std::nullopt;
        return doc.stats.reservoir.front().template_id;
    }
} // namespace

// ── THE POSITIVE SIDE OF THE BOUNDARY: no cap binds, so associativity HOLDS. ──────────────────
//
// This arm exists so the boundary is asserted from BOTH sides. Without it, "associativity
// diverges" would be equally satisfied by a composer that diverges for some unrelated reason — a
// merge that lost an entry, a re-derivation that ran at the wrong scope, a tie-break that became
// argument-ordered. Here the SAME three documents, differing from the arm below only in the cap
// they declare, compose to bracket-INDEPENDENT results. So the divergence below is attributable
// to the admission bound and to nothing else.
//
// IT ALSO CARRIES THE MECHANISM, and this is its second job. The scope-dependent ranking key is
// OBSERVABLE only where both salient templates survive to be read: at the narrow rung the benign
// off-path template outranks the error (75×100 = 7500 > 80×90 = 7200) and at the full rung the
// order INVERTS (80×100 = 8000 > 7500). That inversion is why a cap can break associativity at
// all, and it is measured here rather than assumed by the capped arm below.
TEST(ComposeAlgebraTest, ComposedReservoirStaysAssociativeWhenNoCapBinds)
{
    constexpr std::string_view kOffPath{"took alternate cache path"};
    constexpr std::string_view kError{"connection refused to db"};
    const auto off_path_id{insight::template_id_of(kOffPath)};
    const auto error_id{insight::template_id_of(kError)};

    const auto doc_a{make_a(ReservoirSeed{.tmpl = kOffPath,
                                          .count = 2,
                                          .structural_surprise = 75,
                                          .level = std::nullopt,
                                          .salience = 6750}, // 75 × 90 at A's own 1202 lines
                            kSlackCap)};
    const auto doc_b{make_b(ReservoirSeed{.tmpl = kError,
                                          .count = 3,
                                          .structural_surprise = 0,
                                          .level = insight::LogLevel::Error,
                                          .salience = 7200}, // 80 × 90 at B's own 1203 lines
                            kSlackCap)};
    const auto doc_c{make_c()};

    // ── ① THE CAP CANNOT BIND: more slots than candidates, asserted rather than assumed. ──
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.window.lines_observed, 2405U) << "the narrow rung's line total is the band input";
    ASSERT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{kSlackCap})
        << "min(8, 8) = 8 — the composed document declares its own cap (DN-56.D2)";
    ASSERT_EQ(ab.stats.reservoir.size(), 2U)
        << "both salient templates must survive, or this arm proves nothing about a cap that does "
           "not bind.\n    "
        << render_reservoir(ab);

    // ── ② THE MECHANISM: the ranking key moves with merge scope, and the order inverts. ──
    const auto off_path_narrow{salience_at(ab, off_path_id, "the off-path template")};
    const auto error_narrow{salience_at(ab, error_id, "the error template")};
    EXPECT_EQ(off_path_narrow, 7500U)
        << "kBandOffPath 75 × kRarityRare 100 (2·1000 = 2000 < 2405, so < 0.1 %); got "
        << off_path_narrow << "\n    " << render_reservoir(ab);
    EXPECT_EQ(error_narrow, 7200U)
        << "kBandError 80 × kRarityUncommon 90 (3·1000 = 3000 > 2405 but 3·100 = 300 < 2405, so "
           "below 1 % and not below 0.1 %); got "
        << error_narrow << "\n    " << render_reservoir(ab);
    EXPECT_GT(off_path_narrow, error_narrow)
        << "at the narrow rung the benign off-path template outranks the error";
    EXPECT_EQ(leading_reservoir_id(ab), std::optional{off_path_id})
        << "and it therefore leads the emitted array.\n    " << render_reservoir(ab);

    const auto abc{meta::compose(ab, doc_c)};
    ASSERT_EQ(abc.window.lines_observed, 3605U) << "the full rung's line total is the band input";
    const auto off_path_wide{salience_at(abc, off_path_id, "the off-path template")};
    const auto error_wide{salience_at(abc, error_id, "the error template")};
    EXPECT_EQ(off_path_wide, 7500U) << "unchanged: 2·1000 = 2000 was already below 3605";
    EXPECT_EQ(error_wide, 8000U)
        << "kBandError 80 × kRarityRare 100 — 3·1000 = 3000 < 3605 crossed the 0.1 % threshold; "
           "got "
        << error_wide << "\n    " << render_reservoir(abc);
    EXPECT_GT(error_wide, off_path_wide)
        << "at the full rung the order INVERTS. This is the whole reason a cap breaks "
           "associativity: the ranking key is scope-dependent by design (§12.1 re-derives salience "
           "over the merged counts), so a low-rung cut is not a cut the high rung would have made.";

    // ── ③ ASSOCIATIVITY, and it must HOLD here. ──
    const auto bc{meta::compose(doc_b, doc_c)};
    const auto a_bc{meta::compose(doc_a, bc)};
    EXPECT_EQ(abc.window.lines_observed, a_bc.window.lines_observed);
    EXPECT_EQ(abc.stats.unique_templates, a_bc.stats.unique_templates)
        << "with nothing dropped, no template leaves the document, so both bracketings see the "
           "same union: (A∘B)∘C = "
        << abc.stats.unique_templates << " vs A∘(B∘C) = " << a_bc.stats.unique_templates;
    EXPECT_EQ(abc.stats.unique_templates, 8U)
        << "6 fillers + the off-path + the error, pinned as a VALUE so an equality that held "
           "because both sides collapsed cannot pass";
    const std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>> expected{
        {error_id, 3U, 8000U}, {off_path_id, 2U, 7500U}};
    EXPECT_EQ(reservoir_signature(abc), expected)
        << "the full rung ranks the error first (8000 > 7500) and keeps both.\n    (A∘B)∘C: "
        << render_reservoir(abc);
    EXPECT_EQ(reservoir_signature(abc), reservoir_signature(a_bc))
        << "SPEC §12.2 associativity (SHOULD) over the composed reservoir. With no binding cap, "
           "membership is the union of unions and the final salience is re-derived at a total "
           "scope both bracketings share.\n"
        << "    (A∘B)∘C: " << render_reservoir(abc) << "\n"
        << "    A∘(B∘C): " << render_reservoir(a_bc);
}

// ── THE NEGATIVE SIDE: the cap binds, and the divergence is EXACTLY what DN-56.D3 predicts. ───
//
// Band pair (kBandOffPath 75, kBandError 80) — DN-56.D3's STRICT flip (ratio 1.067, inside the
// 0.9·sev_Q < sev_P < sev_Q window). Same three documents as the arm above; the ONLY change is
// that A and B declare a reservoir cap of 1, so the composed document declares 1 too and has one
// slot for two salient templates.
//
// THE ARITHMETIC, DERIVED — every number below follows from the band ladder and the line totals,
// and none is read off a run:
//
//   A∘B   (2 405 lines, cap 1): off-path 75×100 = 7500 beats error 80×90 = 7200.
//                               The cap keeps the OFF-PATH; the error's count 3 folds into
//                               tail_count and leaves the document (§3.7.3's compose-lossy tail).
//   (A∘B)∘C (3 605 lines):      only the off-path is still a candidate → 75×100 = 7500.
//   B∘C   (2 403 lines, cap 1): the error is the only candidate → 80×90 = 7200, kept.
//   A∘(B∘C) (3 605 lines):      error 80×100 = 8000 now beats off-path 75×100 = 7500, so the cap
//                               keeps the ERROR.
//
// So the two bracketings emit DIFFERENT templates, at different counts, at different saliences —
// and disagree on the REQUIRED field `stats.unique_templates` (7 vs 8). That is the disclosure
// §12.2 owes, stated as arithmetic. If the implementation diverges any other way, this arm reds.
TEST(ComposeAlgebraTest, ComposedReservoirCapBreaksAssociativityExactlyAsDN56D3Rules)
{
    constexpr std::string_view kOffPath{"took alternate cache path"};
    constexpr std::string_view kError{"connection refused to db"};
    const auto off_path_id{insight::template_id_of(kOffPath)};
    const auto error_id{insight::template_id_of(kError)};

    const auto doc_a{make_a(ReservoirSeed{.tmpl = kOffPath,
                                          .count = 2,
                                          .structural_surprise = 75,
                                          .level = std::nullopt,
                                          .salience = 6750},
                            kBindingCap)};
    const auto doc_b{make_b(ReservoirSeed{.tmpl = kError,
                                          .count = 3,
                                          .structural_surprise = 0,
                                          .level = insight::LogLevel::Error,
                                          .salience = 7200},
                            kBindingCap)};
    const auto doc_c{make_c()};

    // ── ① THE COMPOSED DOCUMENT DECLARES ITS OWN CAP, and the cap BINDS. ──
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.window.lines_observed, 2405U);
    EXPECT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{kBindingCap})
        << "min(1, 1) = 1 — DN-56.D2: a composed document declares its own cap, so SPEC §8 "
           "clause 4 now makes a checkable claim about the array (it made none before).\n    "
        << render_reservoir(ab);
    ASSERT_EQ(ab.stats.reservoir.size(), 1U)
        << "two salient candidates, one slot — the cut is what the rest of this arm is about.\n    "
        << render_reservoir(ab);

    // ── ② WHICH ENTRY THE CUT KEEPS, and where the other one goes. ──
    EXPECT_EQ(leading_reservoir_id(ab), std::optional{off_path_id})
        << "at 2 405 lines the off-path scores 75×100 = 7500 and the error 80×90 = 7200, so the "
           "single slot goes to the off-path.\n    "
        << render_reservoir(ab);
    EXPECT_EQ(salience_at(ab, off_path_id, "the off-path template"), 7500U);
    EXPECT_EQ(reservoir_find(ab, error_id), nullptr)
        << "and the error is GONE from the reservoir.\n    " << render_reservoir(ab);
    EXPECT_EQ(ab.stats.tail_count, 3U)
        << "its whole count is now lumped tail mass — which is precisely why it can never re-enter "
           "at a wider scope: the composed document no longer carries it as a template. Got "
        << ab.stats.tail_count << " (expected exactly the error's count of 3).";
    EXPECT_EQ(ab.stats.tail_unique, 1U) << "one template folded, no others";

    // ── ③ THE DIVERGENCE, both bracketings pinned as VALUES. ──
    const auto abc{meta::compose(ab, doc_c)};
    const auto bc{meta::compose(doc_b, doc_c)};
    const auto a_bc{meta::compose(doc_a, bc)};
    ASSERT_EQ(abc.window.lines_observed, 3605U);
    ASSERT_EQ(a_bc.window.lines_observed, 3605U)
        << "both bracketings must reach the SAME total scope, or the divergence below is about "
           "line counts rather than about the cap";
    EXPECT_EQ(bc.stats.reservoir_size, std::optional<std::size_t>{kBindingCap})
        << "C declares no cap at all, and an absent declaration is skipped rather than read as a "
           "bound of zero — so B∘C carries B's 1 (DN-56.D2). Got "
        << (bc.stats.reservoir_size ? std::to_string(*bc.stats.reservoir_size)
                                    : std::string{"<absent>"});

    const std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>> left_expected{
        {off_path_id, 2U, 7500U}};
    const std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>> right_expected{
        {error_id, 3U, 8000U}};
    EXPECT_EQ(reservoir_signature(abc), left_expected)
        << "(A∘B)∘C: the error was already cut at the narrow rung, so the off-path is the only "
           "candidate left and keeps its 75×100 = 7500.\n    "
        << render_reservoir(abc);
    EXPECT_EQ(reservoir_signature(a_bc), right_expected)
        << "A∘(B∘C): the error survived B∘C at 7200 and, re-derived at 3 605 lines, scores "
           "80×100 = 8000 — beating the off-path's 7500 and taking the single slot.\n    "
        << render_reservoir(a_bc);
    EXPECT_NE(reservoir_signature(abc), reservoir_signature(a_bc))
        << "SPEC §12.2 associativity (SHOULD) DOES NOT HOLD under a binding cap, and that is "
           "DN-56.D3's ruled and disclosed trade, not a regression. If this arm ever reds by the "
           "two sides AGREEING, associativity was restored — check whether the cap was removed "
           "(the thing DN-56.D2 forbids) before believing it is an improvement.\n"
        << "    (A∘B)∘C: " << render_reservoir(abc) << "\n"
        << "    A∘(B∘C): " << render_reservoir(a_bc);

    // ── ④ AND IT REACHES A REQUIRED FIELD, which DN-056's prose does not say. ──
    // `stats.unique_templates` is REQUIRED by the published schema. DN-56.D3 argues associativity
    // may yield because what yields is an unwritten property of an OPTIONAL block; measured, the
    // cut also moves a required one, because a cut entry leaves the document entirely and the next
    // rung's union cannot see it. The disclosure §12.2 owes is therefore wider than D3 states.
    EXPECT_EQ(abc.stats.unique_templates, 7U)
        << "6 fillers + the off-path; the error is not in the union because A∘B dropped it. Got "
        << abc.stats.unique_templates;
    EXPECT_EQ(a_bc.stats.unique_templates, 8U)
        << "6 fillers + the off-path + the error, which B∘C kept. Got "
        << a_bc.stats.unique_templates;
    EXPECT_NE(abc.stats.unique_templates, a_bc.stats.unique_templates)
        << "the SAME three documents, bracketed two ways, disagree on a REQUIRED field";
}

// ── THE TIE-BREAK PAIR: the cut is decided by a MEANING-BLIND hash. ───────────────────────────
//
// Band pair (kBandStrongOffPath 90, kBandFatal 100). The flip condition is 0.9·sev_Q < sev_P <
// sev_Q and this pair sits exactly ON the lower edge: 0.9 × 100 = 90 is not strictly less than 90.
// So the narrow rung is an EXACT TIE (90×100 = 9000 = 100×90) and the wide rung is a strict win
// for the fatal (100×100 = 10000). (DN-56.D3's prose places the tie at the WIDENED rung; the
// arithmetic puts it at the NARROW one, and the arithmetic is written out here.)
//
// WHAT THIS ARM PROVES, and it is not a second copy of the strict-flip arm. Under a cap of 1 an
// exact tie means the single slot is allocated by `template_id` ascending (§3.7.2.1) — a content
// hash that is deterministic and MEANING-BLIND. So which of a FATAL and a benign off-path template
// survives into every wider scope is decided by a hash, and DN-56.D3's "including, half the time,
// the FATAL" becomes a measured statement rather than a rhetorical one.
//
// AND THE HASH ORDER DECIDES WHETHER ASSOCIATIVITY HOLDS AT ALL HERE, which is the sharpest thing
// this arm has to say. A∘(B∘C) always lands on the fatal (it wins the wide rung outright at
// 10000). (A∘B)∘C lands on whichever the narrow tie-break kept. With today's ids the tie-break
// keeps the FATAL, so the two bracketings AGREE — associativity survives this pair by luck, not by
// property. Were the order reversed, (A∘B)∘C would carry the off-path at 90×100 = 9000 and the
// fatal would be the entry lost. The order is pinned below precisely so that a future change to
// canon's template_id hash reds this arm and forces the expectations to be RE-DERIVED rather than
// silently re-pinned to whichever case became live.
TEST(ComposeAlgebraTest, ComposedReservoirCapResolvesAnExactTieByTheMeaningBlindTemplateId)
{
    constexpr std::string_view kStrongOffPath{"took alternate cache path"};
    constexpr std::string_view kFatal{"connection refused to db"};
    const auto off_path_id{insight::template_id_of(kStrongOffPath)};
    const auto fatal_id{insight::template_id_of(kFatal)};

    const ReservoirSeed off_path_seed{.tmpl = kStrongOffPath,
                                      .count = 2,
                                      .structural_surprise = 90,
                                      .level = std::nullopt,
                                      .salience = 8100}; // 90 × 90 at A's own 1202 lines
    const ReservoirSeed fatal_seed{.tmpl = kFatal,
                                   .count = 3,
                                   .structural_surprise = 0,
                                   .level = insight::LogLevel::Fatal,
                                   .salience = 9000}; // 100 × 90 at B's own 1203 lines
    const auto doc_c{make_c()};

    // ── ① THE TIE IS REAL, observed where BOTH entries survive. ──
    // A cap of 1 emits only one of them, so the tie itself cannot be read off the capped document.
    // The same pair is composed at a slack cap first: that control is what stops this arm from
    // passing on a fixture where one template simply outranked the other.
    const auto slack{
        meta::compose(make_a(off_path_seed, kSlackCap), make_b(fatal_seed, kSlackCap))};
    ASSERT_EQ(slack.window.lines_observed, 2405U);
    ASSERT_EQ(slack.stats.reservoir.size(), 2U) << render_reservoir(slack);
    EXPECT_EQ(salience_at(slack, off_path_id, "the strong-off-path template"), 9000U)
        << "kBandStrongOffPath 90 × kRarityRare 100 (2·1000 = 2000 < 2405)";
    EXPECT_EQ(salience_at(slack, fatal_id, "the fatal template"), 9000U)
        << "kBandFatal 100 × kRarityUncommon 90 (3·1000 = 3000 > 2405, 3·100 = 300 < 2405)";

    // ── ② UNDER THE CAP, THE TIE-BREAK ALLOCATES THE SINGLE SLOT. ──
    const auto doc_a{make_a(off_path_seed, kBindingCap)};
    const auto doc_b{make_b(fatal_seed, kBindingCap)};
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.stats.reservoir.size(), 1U)
        << "an exact tie and one slot — one of the two salient templates is dropped.\n    "
        << render_reservoir(ab);
    EXPECT_EQ(leading_reservoir_id(ab), std::optional{std::min(off_path_id, fatal_id)})
        << "§3.7.2.1: an exact salience tie resolves to the LOWER template_id, never to argument "
           "order. A red here means the tie-break became insertion-ordered — which would also make "
           "compose() non-commutative.\n    "
        << render_reservoir(ab);

    // The order of two content hashes, pinned as the FACT the derivation below rests on. It is an
    // input to the arithmetic, exactly like the band constants — not an expectation about design.
    ASSERT_LT(fatal_id, off_path_id)
        << "the fatal's template_id sorts BELOW the off-path's, so today's tie-break keeps the "
           "fatal and the two bracketings below AGREE. If canon's template_id hash changes this "
           "order, the survivor becomes the benign off-path at 90×100 = 9000, (A∘B)∘C and "
           "A∘(B∘C) DIVERGE, and the FATAL is the entry lost — re-derive the expectations below "
           "from that arithmetic, never re-pin them to whatever the run produced. "
        << insight::render(fatal_id) << " vs " << insight::render(off_path_id);
    EXPECT_EQ(reservoir_find(ab, off_path_id), nullptr)
        << "so the off-path is the entry the meaning-blind hash dropped.\n    "
        << render_reservoir(ab);
    EXPECT_EQ(ab.stats.tail_count, 2U) << "its count folds into the tail and leaves the document";

    // ── ③ THE CONSEQUENCE FOR ASSOCIATIVITY, derived from ② and NOT a property of the design. ──
    const auto abc{meta::compose(ab, doc_c)};
    const auto bc{meta::compose(doc_b, doc_c)};
    const auto a_bc{meta::compose(doc_a, bc)};
    const std::vector<std::tuple<insight::TemplateId, std::uint64_t, std::uint32_t>> fatal_at_wide{
        {fatal_id, 3U, 10000U}};
    EXPECT_EQ(reservoir_signature(a_bc), fatal_at_wide)
        << "A∘(B∘C): the fatal survives B∘C (sole candidate, 100×90 = 9000) and wins the wide rung "
           "outright — kBandFatal 100 × kRarityRare 100 (3·1000 = 3000 < 3605), the ceiling of the "
           "ladder.\n    "
        << render_reservoir(a_bc);
    EXPECT_EQ(reservoir_signature(abc), fatal_at_wide)
        << "(A∘B)∘C: the narrow tie-break happened to keep the fatal, so re-deriving it at 3 605 "
           "lines gives the same 10000 — the bracketings AGREE here, and the agreement is an "
           "accident of a content hash, not associativity holding.\n    "
        << render_reservoir(abc);
    EXPECT_EQ(reservoir_signature(abc), reservoir_signature(a_bc))
        << "stated explicitly so the arm's verdict is not left to be inferred from the two "
           "assertions above.\n    (A∘B)∘C: "
        << render_reservoir(abc) << "\n    A∘(B∘C): " << render_reservoir(a_bc);

    // And the cost is still paid on a REQUIRED field, exactly as in the strict-flip arm: the
    // dropped off-path never reaches the wider union, whichever entry the hash kept.
    EXPECT_EQ(abc.stats.unique_templates, 7U)
        << "6 fillers + the fatal; the off-path left the document at the narrow rung. Got "
        << abc.stats.unique_templates;
    EXPECT_EQ(a_bc.stats.unique_templates, 8U)
        << "6 fillers + the fatal + the off-path, which never met a binding cut on this side. Got "
        << a_bc.stats.unique_templates;
}

// ── SPEC §12.1 — `C.stats.entropy_bits` IS RECOMPUTED FROM THE MERGED COUNTS ──────────────────
//
// A §12.1 clause, not one of §12.2's three, and it is deliberately its own arm. §12.1 states
// "`C.stats.entropy_bits` is recomputed from the merged counts" in the indicative and `compose()`
// simply never assigned the field, so every composed document omitted it — DN-56.D5's exact shape
// on a different field, and GOVERNANCE §3 decides it the same way: the rule is derivable from the
// inputs and correct, so the reference implementation was buggy and the spec text stands.
//
// WHY NOT FOLDED INTO THE IDENTITY ARM. Identity would catch it (A's tail is empty, so the merged
// counts ARE A's counts), but identity owns the composed-CAP fix and a single red must never be
// ambiguous about which of two independent clauses moved. The identity leg lives here instead, as
// ③ below.
//
// THE ORACLE IS CLOSED-FORM ARITHMETIC, never a second entropy computation. Every count and line
// total below is a power of two, so `det_log2_fixed` is exact (a power-of-two mantissa normalises
// to 1.0 and every squaring step emits a zero fraction bit) and the whole reduction is an exact
// integer divide — the expected values are not approximations.
TEST(ComposeAlgebraTest, ComposedEntropyBitsIsRecomputedFromTheMergedCounts)
{
    // ── ① OVER THE MERGED COUNTS, NOT OVER THE RETAINED top_k. ──
    // Four templates at 64 each over 256 merged lines is uniform over 4 → exactly log2(4) = 2 bits.
    // `top_k_size` is 2 on both inputs, so the composed document PUBLISHES only two of them and
    // lumps the rest; the entropy must still describe all four, exactly as the producer's own
    // `entropy_bits` describes its full untruncated distribution.
    const auto left{make_document("2026-02-01T00:00:00Z", "2026-02-01T00:01:00Z", 128, 2,
                                  std::nullopt, {{"entropy alpha", 64}, {"entropy beta", 64}}, {})};
    const auto right{make_document("2026-02-01T00:01:00Z", "2026-02-01T00:02:00Z", 128, 2,
                                   std::nullopt, {{"entropy gamma", 64}, {"entropy delta", 64}},
                                   {})};
    const auto merged{meta::compose(left, right)};
    ASSERT_EQ(merged.window.lines_observed, 256U);
    ASSERT_EQ(merged.stats.unique_templates, 4U) << "four distinct templates in the union";
    ASSERT_EQ(merged.stats.top_k.size(), 2U)
        << "the top_k cut must BITE, or this leg cannot tell 'over the merged counts' from 'over "
           "the retained top_k'.\n    "
        << render_top_k(merged);
    ASSERT_EQ(merged.stats.tail_unique, 2U) << "the other two are lumped into the tail";
    ASSERT_TRUE(merged.stats.entropy_bits.has_value())
        << "§12.1 orders the field recomputed; an absent one is the clause unimplemented";
    EXPECT_DOUBLE_EQ(*merged.stats.entropy_bits, 2.0)
        << "uniform over 4 templates at 64/256 each → log2(4) = 2 bits exactly. Over the two "
           "RETAINED entries alone it would be (64·(8−6) + 64·(8−6))/256 = 1.0, so this value is "
           "what separates the clause from a top_k-only reading. Got "
        << *merged.stats.entropy_bits;

    // ── ② THE LOST TAIL MASS ENTERS AS ONE RESIDUAL BUCKET. ──
    // §12.3 makes composition lossy on a tailed input: those lines are counted in `lines_observed`
    // but no longer attributable to any template. Dropping them normalises over a denominator the
    // counts never reach (over-stating concentration); attributing them to a template would invent
    // a fact. One residual bucket is the same convention `tail_summary` already uses.
    auto heavy_left{make_document("2026-02-01T00:00:00Z", "2026-02-01T00:01:00Z", 256, 4,
                                  std::nullopt, {{"entropy heavy left", 128}}, {})};
    heavy_left.stats.tail_count = 128;
    heavy_left.stats.tail_unique = 2;
    heavy_left.stats.unique_templates = 3;
    auto heavy_right{make_document("2026-02-01T00:01:00Z", "2026-02-01T00:02:00Z", 256, 4,
                                   std::nullopt, {{"entropy heavy right", 128}}, {})};
    heavy_right.stats.tail_count = 128;
    heavy_right.stats.tail_unique = 2;
    heavy_right.stats.unique_templates = 3;
    const auto tailed{meta::compose(heavy_left, heavy_right)};
    ASSERT_EQ(tailed.window.lines_observed, 512U);
    ASSERT_EQ(tailed.stats.tail_count, 256U) << "both inputs' lumped masses carry across";
    ASSERT_TRUE(tailed.stats.entropy_bits.has_value());
    EXPECT_DOUBLE_EQ(*tailed.stats.entropy_bits, 1.5)
        << "counts {128, 128} plus a residual of 256, over 512 lines: (128·(9−7) + 128·(9−7) + "
           "256·(9−8))/512 = 768/512 = 1.5 bits. WITHOUT the residual bucket it would be "
           "(128·2 + 128·2)/512 = 1.0, so this value is what pins the residual convention. Got "
        << *tailed.stats.entropy_bits;

    // ── ③ THE IDENTITY LEG. compose(A, ZERO) must reproduce A's entropy EXACTLY. ──
    // A has an empty tail, so the merged counts ARE A's counts and the two reductions run over the
    // same multiset at the same denominator. Exact equality, not a tolerance: the accumulation is
    // integer and order-independent by construction.
    const meta::MetaLogConfig cfg{
        .top_k_size = 3,
        .reservoir_size = 8,
        .top_ngrams_size = 0,
        .emit_stability = false,
        .max_param_histograms = 0,
    };
    meta::MetaLogEngine engine_a{cfg};
    engine_a.open_window(kT0);
    for (int rep = 0; rep < 100; ++rep)
    {
        engine_a.ingest_event(make_event("alpha steady event"));
        engine_a.ingest_event(make_event("beta steady event"));
        engine_a.ingest_event(make_event("gamma steady event"));
    }
    engine_a.ingest_event(make_event("connection refused to db", insight::LogLevel::Error));
    const auto doc_a{engine_a.close_window(kT1)};
    meta::MetaLogEngine engine_z{cfg};
    engine_z.open_window(kT0);
    const auto zero{engine_z.close_window(kT1)};

    ASSERT_EQ(doc_a.stats.tail_unique, 0U)
        << "A must have an EMPTY tail, or §12.3 lossiness — not the recomputation — explains a red";
    ASSERT_TRUE(doc_a.stats.entropy_bits.has_value())
        << "A must carry entropy_bits or this leg is about an absent input";
    const auto identity{meta::compose(doc_a, zero)};
    EXPECT_EQ(identity.stats.entropy_bits, doc_a.stats.entropy_bits)
        << "§12.2 identity over §12.1's recomputed field: A = "
        << (doc_a.stats.entropy_bits ? std::to_string(*doc_a.stats.entropy_bits)
                                     : std::string{"<absent>"})
        << ", compose(A, ZERO) = "
        << (identity.stats.entropy_bits ? std::to_string(*identity.stats.entropy_bits)
                                        : std::string{"<absent>"});

    // ── ④ AND AN EVENT-FREE COMPOSITION HAS NO DISTRIBUTION TO DESCRIBE. ──
    // The producer omits the field when lines_observed is 0; compose() must make the same call
    // rather than emitting a 0.0 that would read as "perfectly concentrated".
    const auto empty{meta::compose(zero, zero)};
    ASSERT_EQ(empty.window.lines_observed, 0U);
    EXPECT_FALSE(empty.stats.entropy_bits.has_value())
        << "zero lines is an absent distribution, not an entropy of zero";
}

// ── SPEC §12.2 ASSOCIATIVITY — THE FALSIFIER FOR DN-56.D8 PROBLEM (e) ────────────────────────
//
// WHAT THESE TWO ARMS DECIDE, AND WHY THEY ARE NOT A DUPLICATE OF THE CAPPED PAIR ABOVE.
// The `0.10.0` RFC body asserts to EXTERNAL implementers of a public, vendor-neutral standard
// that §12.2's associativity clause is violated BY THE FORMAT, not merely by our composed
// reservoir cap. Problem (e) rests on that sentence. If it were false — if only our own cap broke
// §12.2 — the claim would have to be retracted to exactly the audience `metalog-spec` exists to
// serve. So the sentence needs a falsifier that runs, and the falsifier has to reach the property
// with the reservoir mechanism ENTIRELY OUT OF THE PICTURE.
//
// THESE DOCUMENTS DECLARE NO RESERVOIR CAP AND CARRY NO RESERVOIR ENTRY. Not "a cap so slack it
// cannot bind" — none at all. `min_declared_cap(absent, absent)` is absent, the composed
// admission bound is `res_cands.size()`, and `res_cands` is built from the union of the inputs'
// reservoir arrays, which are empty. Every line of `rederive_reservoir` is therefore inert here
// and cannot be what any divergence below is about. The ONE remaining lossy operator is
// `top_k_size` truncation, which is §3.5's oldest mechanism and predates DN-56 entirely.
//
// THE MECHANISM, IN ONE SENTENCE: a cut entry leaves the document ENTIRELY. `compose()` rebuilds
// its distribution from the inputs' carried per-template arrays (`aggregate_top_k` +
// `aggregate_reservoir`), so a template that fell below an EARLIER rung's `top_k_size` is no
// longer a template at the next rung — only its mass survives, inside `tail_count`. Mass is
// associative; a SET is not. Bracketing decides which rungs a template must survive, so it
// decides the membership of the final distribution, and `stats.unique_templates` is REQUIRED.
//
// NO TIE-BREAK IS LOAD-BEARING HERE, DELIBERATELY. The four counts are 10 / 9 / 8 / 1, all
// distinct, so `state.ordered`'s sort never reaches its `template_id` tie-break and no
// `template_id` hash decides any outcome below. A sibling arm in this file
// (…ResolvesAnExactTieByThe MeaningBlindTemplateId) exists precisely because ITS pair does turn on
// a hash; this one must not, or the RFC's claim about the FORMAT would be a claim about our
// content-hash instead.

// ARM 1 — THE DIVERGENCE. `(A∘B)∘C` and `A∘(B∘C)` disagree on a REQUIRED field with no reservoir
// anywhere, so DN-56.D8 (e) stands: §12.2's associativity SHOULD is broken by the format's own
// top_k truncation, independently of anything DN-56 introduced.
//
// THE ARITHMETIC, DERIVED — every number written out, none transcribed from a run.
//   A = {a:10, b:1} over 11 lines · B = {c:9} over 9 · C = {d:8} over 8 · top_k_size 2 everywhere.
//   A∘B      : ordered a:10, c:9, b:1 → unique 3; the cut at 2 keeps {a, c} and drops b into
//              tail_count=1, tail_unique=1. b IS NO LONGER A TEMPLATE.
//   (A∘B)∘C  : ordered a:10, c:9, d:8       → unique 3. b cannot re-enter: AB carries no entry
//              for it, only the lumped 1. Cut at 2 → tail d:8; tail_count = 8 + 1(AB's) = 9.
//   B∘C      : ordered c:9, d:8             → unique 2; nothing is cut (2 entries, cut at 2).
//   A∘(B∘C)  : ordered a:10, c:9, d:8, b:1  → unique 4. b survived A's own cut (A has exactly 2
//              templates at top_k_size 2), so it reaches the final merge as a TEMPLATE.
//              Cut at 2 → tail {d:8, b:1}; tail_count = 9, tail_unique = 2.
//   So unique_templates is 3 vs 4 and tail_unique is 1 vs 2, while lines_observed (28) and
//   tail_count (9) AGREE. That agreement is the load-bearing half: no mass is lost or invented,
//   so the divergence cannot be dismissed as a counting bug — it is the loss of IDENTITY, which
//   is precisely what §12.2's SHOULD is about.
//
// Reddens if: `compose()` ever starts carrying a cut template's identity across a rung (which
// would make (e) wrong and is the outcome the RFC must be re-derived against), or if
// `unique_templates` stops being `state.ordered.size()`.
TEST(ComposeAlgebraTest, TopKTruncationAloneBreaksAssociativityWithNoReservoirAnywhere)
{
    constexpr std::size_t kCut{2};
    const auto a{make_document("2026-03-01T00:00:00Z", "2026-03-01T00:01:00Z", 11, kCut,
                               std::nullopt, {{"assoc alpha", 10}, {"assoc beta", 1}}, {})};
    const auto b{make_document("2026-03-01T00:01:00Z", "2026-03-01T00:02:00Z", 9, kCut,
                               std::nullopt, {{"assoc gamma", 9}}, {})};
    const auto c{make_document("2026-03-01T00:02:00Z", "2026-03-01T00:03:00Z", 8, kCut,
                               std::nullopt, {{"assoc delta", 8}}, {})};

    // The mechanism assertion FIRST: if the reservoir machinery were live on these inputs, nothing
    // below would be a statement about top_k truncation.
    ASSERT_TRUE(a.stats.reservoir.empty() && b.stats.reservoir.empty() && c.stats.reservoir.empty())
        << "the falsifier requires NO reservoir entries — the reservoir must be out of the picture";
    ASSERT_FALSE(a.stats.reservoir_size.has_value())
        << "and NO declared cap: an absent cap is not a claim (§8 clause 4), so `min_declared_cap` "
           "returns absent and the composed admission bound is the candidate count itself";

    const auto ab{meta::compose(a, b)};
    ASSERT_EQ(ab.stats.top_k_size, kCut);
    ASSERT_EQ(ab.stats.unique_templates, 3U)
        << "a, c and b all merge in:\n    " << render_top_k(ab);
    ASSERT_EQ(ab.stats.top_k.size(), 2U)
        << "the cut must BITE at this rung, or the whole arm is vacuous:\n    " << render_top_k(ab);
    ASSERT_EQ(ab.stats.tail_unique, 1U) << "b is cut here — this is the irreversible step";
    ASSERT_EQ(ab.stats.tail_count, 1U) << "and its whole mass (1) becomes lumped tail";
    ASSERT_FALSE(ab.stats.reservoir_size.has_value())
        << "neither input declared a cap, so the composed document declares none either";
    ASSERT_TRUE(ab.stats.reservoir.empty()) << "and admits nothing — there were no candidates";

    const auto ab_c{meta::compose(ab, c)};
    const auto bc{meta::compose(b, c)};
    ASSERT_EQ(bc.stats.unique_templates, 2U) << "c and d only — nothing is cut on this side";
    const auto a_bc{meta::compose(a, bc)};

    // ── The two totals AGREE: no mass is lost or invented by either bracketing. ──
    EXPECT_EQ(ab_c.window.lines_observed, 28U);
    EXPECT_EQ(a_bc.window.lines_observed, 28U) << "11 + 9 + 8 either way";
    EXPECT_EQ(ab_c.stats.tail_count, 9U)
        << "8 (d) + 1 (AB's lumped b). Got " << ab_c.stats.tail_count;
    EXPECT_EQ(a_bc.stats.tail_count, 9U)
        << "8 (d) + 1 (b, still a template until this cut). Got " << a_bc.stats.tail_count;

    // ── And the SETS do not. This is DN-56.D8 problem (e), measured. ──
    EXPECT_EQ(ab_c.stats.unique_templates, 3U)
        << "(A∘B)∘C sees a, c, d — b left the document at the A∘B rung and cannot re-enter.\n    "
        << render_top_k(ab_c);
    EXPECT_EQ(a_bc.stats.unique_templates, 4U)
        << "A∘(B∘C) sees a, c, d AND b — b never met a binding cut before the final merge.\n    "
        << render_top_k(a_bc);
    EXPECT_NE(ab_c.stats.unique_templates, a_bc.stats.unique_templates)
        << "§12.2's associativity SHOULD, violated on a REQUIRED field with the reservoir "
           "mechanism entirely inert: (A∘B)∘C = "
        << ab_c.stats.unique_templates << ", A∘(B∘C) = " << a_bc.stats.unique_templates
        << ". If these ever agree, DN-56.D8 problem (e) is WRONG and the `0.10.0` RFC body must be "
           "re-derived before it is posted.";
    EXPECT_EQ(ab_c.stats.tail_unique, 1U) << "only d is a visible tail template on this side";
    EXPECT_EQ(a_bc.stats.tail_unique, 2U)
        << "d and b are both visible tail templates on this side — the cardinality diverges too, "
           "because an earlier fold destroyed b's identity, not merely its rank";
}

// ARM 2 — THE CONTROL, and it is what makes ARM 1 attributable. THE SAME THREE DOCUMENTS at a
// `top_k_size` that cannot cut are perfectly associative: every required field agrees. So the
// divergence above is caused by the truncation and by nothing else in `compose()` — not by the
// merge order, not by the line-total denominators, not by any residual of the reservoir path.
//
// A boundary asserted from one side only is a golden re-pinned for green; this is the other side.
//
//   top_k_size 4 · A = {a:10, b:1} · B = {c:9} · C = {d:8}
//   A∘B = {a:10, c:9, b:1}, cut at min(4,3)=3 → nothing lumped, tail empty.
//   Both bracketings therefore end at the full merged distribution {a:10, c:9, d:8, b:1} over 28
//   lines, with tail_count 0 and tail_unique 0.
TEST(ComposeAlgebraTest, TheSameDocumentsStayAssociativeWhenTheTopKCutDoesNotBite)
{
    constexpr std::size_t kSlack{4};
    const auto a{make_document("2026-03-01T00:00:00Z", "2026-03-01T00:01:00Z", 11, kSlack,
                               std::nullopt, {{"assoc alpha", 10}, {"assoc beta", 1}}, {})};
    const auto b{make_document("2026-03-01T00:01:00Z", "2026-03-01T00:02:00Z", 9, kSlack,
                               std::nullopt, {{"assoc gamma", 9}}, {})};
    const auto c{make_document("2026-03-01T00:02:00Z", "2026-03-01T00:03:00Z", 8, kSlack,
                               std::nullopt, {{"assoc delta", 8}}, {})};

    const auto ab_c{meta::compose(meta::compose(a, b), c)};
    const auto a_bc{meta::compose(a, meta::compose(b, c))};

    ASSERT_EQ(ab_c.stats.tail_unique, 0U)
        << "no template may be cut on this arm, or it is not a control:\n    "
        << render_top_k(ab_c);
    ASSERT_EQ(a_bc.stats.tail_unique, 0U)
        << "likewise on the other bracketing:\n    " << render_top_k(a_bc);

    EXPECT_EQ(ab_c.stats.unique_templates, 4U) << render_top_k(ab_c);
    EXPECT_EQ(a_bc.stats.unique_templates, 4U) << render_top_k(a_bc);
    EXPECT_EQ(ab_c.stats.unique_templates, a_bc.stats.unique_templates)
        << "same documents, same reservoir posture (none), only the cut removed — associativity "
           "returns. That is what attributes ARM 1's divergence to top_k truncation.";
    EXPECT_EQ(top_k_signature(ab_c), top_k_signature(a_bc))
        << "the emitted top_k must agree as a SEQUENCE, not merely as a set:\n    left:  "
        << render_top_k(ab_c) << "\n    right: " << render_top_k(a_bc);
    EXPECT_EQ(ab_c.window.lines_observed, a_bc.window.lines_observed);
    EXPECT_EQ(ab_c.stats.tail_count, a_bc.stats.tail_count);
    EXPECT_EQ(ab_c.stats.tail_count, 0U) << "nothing was ever lumped";
    EXPECT_EQ(ab_c.stats.entropy_bits, a_bc.stats.entropy_bits)
        << "§12.1's recomputation runs over the same merged multiset on both sides, so it agrees "
           "exactly — an integer reduction over an identically ordered distribution";
}

// ── §12.1 `min` OVER *DECLARED* CAPS, ON THE ORDINARY PATH (DN-56.O3 / DN-56.D2) ──────────────
//
// THE PREMISE DN-56.D2 GOT WRONG, RE-DERIVED HERE AT SOURCE. D2 argued that `retention_profile`
// gating makes `M_A == M_B` "in every normal case, so the question does not even arise", which
// would leave `min_declared_cap`'s absent branch reachable only by the synthetic ZERO document.
// It is false, and the reason is structural rather than exotic: `MetaLogEngine::build_reservoir`
// returns BEFORE its declaration site when `reservoir_size == 0` **OR**
// `analysis.ordered.size() <= analysis.top_k_cut`, and `top_k_cut` is
// `min(top_k_size, ordered.size())` — so that second disjunct is simply "every template fit in
// top-K". A window like that declares NO `reservoir_size` even though the producer was configured
// with one. Same producer, same config, same `retention_profile` stamp: A STAMP GATES VALUES,
// NEVER PRESENCE.
//
// So a real Sift or server run mixes declared-cap and absent-cap documents CONSTANTLY — a quiet
// window folds against a busy one all day long. That pair is the ordinary path, and until this
// arm the only thing exercising the absent branch was `compose(A, ZERO)`, an edge by construction
// whose green can coexist with an ordinary path that is broken.
//
// WHAT REDDENS IT: a `min` that folds absence in as a bound of zero. `min(0, 8) == 0`, the
// composed document would declare a cap of zero, `admission_bound` would be zero, and EVERY
// salient template of the busy window would be silently dropped into the tail on contact with a
// quiet neighbour — the exact multi-scale blindness the reservoir exists to prevent, reachable
// from a completely ordinary pair of windows. The identity arm above would not catch it: ZERO has
// no salient entries to lose.
//
// BOTH INPUTS COME OUT OF ONE REAL `MetaLogEngine` AT ONE CONFIG. Hand-built documents could
// assert the same `min`, but they could not assert the premise — that this producer, so
// configured, really does emit an absent cap on an ordinary window. That premise is the finding,
// so the arm asserts it first and from the producer itself.
TEST(ComposeAlgebraTest, MinOverDeclaredCapsSurvivesASameProducerAbsentCapPair)
{
    constexpr std::size_t kTopK{4};
    constexpr std::size_t kCap{8};
    const meta::MetaLogConfig cfg{
        .top_k_size = kTopK,
        .reservoir_size = kCap,
        .top_ngrams_size = 0,
        .emit_stability = false,
        .max_param_histograms = 0,
    };

    // ── The QUIET window: 3 templates, all of which fit in top-K (cut = min(4,3) = 3), so
    // `build_reservoir` takes its early return and never reaches the declaration site.
    meta::MetaLogEngine quiet_engine{cfg};
    quiet_engine.open_window(kT0);
    for (int rep = 0; rep < 20; ++rep)
    {
        quiet_engine.ingest_event(make_event("quiet alpha steady"));
        quiet_engine.ingest_event(make_event("quiet beta steady"));
        quiet_engine.ingest_event(make_event("quiet gamma steady"));
    }
    const auto quiet{quiet_engine.close_window(kT1)};

    // ── The BUSY window: 4 frequent templates fill top-K and two rare ERRORs fall below it, so
    // the reservoir is built and the cap IS declared. Same engine config, same stamp.
    meta::MetaLogEngine busy_engine{cfg};
    busy_engine.open_window(kT0);
    for (int rep = 0; rep < 20; ++rep)
    {
        busy_engine.ingest_event(make_event("busy alpha steady"));
        busy_engine.ingest_event(make_event("busy beta steady"));
        busy_engine.ingest_event(make_event("busy gamma steady"));
        busy_engine.ingest_event(make_event("busy delta steady"));
    }
    busy_engine.ingest_event(make_event("busy ledger checksum mismatch", insight::LogLevel::Error));
    busy_engine.ingest_event(
        make_event("busy settlement batch rejected", insight::LogLevel::Error));
    const auto busy{busy_engine.close_window(kT1)};

    // ── THE PREMISE, ASSERTED AT THE PRODUCER. If either of these moves, DN-56.O3's finding has
    // changed and this arm's whole reason to exist must be re-derived rather than re-pinned.
    ASSERT_FALSE(quiet.stats.reservoir_size.has_value())
        << "a window whose templates ALL fit in top-K must declare no reservoir cap — that early "
           "return is what makes the absent branch ordinary rather than exotic:\n    "
        << render_reservoir(quiet);
    ASSERT_TRUE(quiet.stats.reservoir.empty()) << render_reservoir(quiet);
    ASSERT_EQ(busy.stats.reservoir_size, kCap)
        << "the SAME producer at the SAME config declares its cap on a window that overflows "
           "top-K — a stamp gates values, never presence:\n    "
        << render_reservoir(busy);
    ASSERT_EQ(busy.stats.reservoir.size(), 2U)
        << "both rare errors must be admitted, or the fold below has nothing to lose:\n    "
        << render_reservoir(busy);

    // ── ABSENT IS SKIPPED, NOT READ AS ZERO — in both bracketings (§12.2 commutativity MUST).
    const auto quiet_busy{meta::compose(quiet, busy)};
    const auto busy_quiet{meta::compose(busy, quiet)};

    EXPECT_EQ(quiet_busy.stats.reservoir_size, kCap)
        << "min(absent, 8) is 8: an input that declares nothing makes no claim (§8 clause 4). A 0 "
           "here is the failure this arm exists for — it would be a document declaring itself "
           "bounded by zero:\n    "
        << render_reservoir(quiet_busy);
    EXPECT_EQ(busy_quiet.stats.reservoir_size, kCap)
        << "and the same from the other side — `min` is symmetric, which is what makes §12.2's "
           "commutativity MUST hold on this field:\n    "
        << render_reservoir(busy_quiet);

    EXPECT_EQ(quiet_busy.stats.reservoir.size(), 2U)
        << "both of the busy window's salient templates SURVIVE the fold with a quiet neighbour. "
           "Under an absent-as-zero `min` this is 0 and every one of them is silently lumped into "
           "the tail — on a pair of windows a real run produces constantly:\n    "
        << render_reservoir(quiet_busy);
    EXPECT_EQ(reservoir_signature(quiet_busy), reservoir_signature(busy_quiet))
        << "membership, merged count and re-derived salience must agree as a SEQUENCE across the "
           "two bracketings:\n    left:  "
        << render_reservoir(quiet_busy) << "\n    right: " << render_reservoir(busy_quiet);
}

// ── WHICH OF THE THREE `min` CAP SITES THE §2.4 GATE LEAVES REACHABLE (DN-56.D2 / P3) ─────────
//
// THE ARM ABOVE PROVED A STAMP GATES VALUES AND NOT PRESENCE. This one asks the question that
// leaves open, and the answer is not the same at all three cap sites: **when the two inputs are
// both STAMPED, `compose()` never reaches the `min` at two of them.**
//
// `retention_profile_name` (metalog.api.cppm) derives the §2.4 stamp from exactly four axes —
// `salience-1/k<top_k_size>-m<reservoir_size>-c<per_kind_cap>-e<error_reserve>` — and it is
// INJECTIVE over that tuple, a property its own suite pins. `compose()`'s gate throws when both
// inputs carry a stamp and the stamps differ. Compose those two facts:
//
//   * `stats.top_k_size` IS axis `k`. Two stamped documents whose `top_k_size` differs carry
//     different stamps, so `compose()`'s §2.4 gate throws BEFORE the top-K `min` runs. That
//     `min` is a no-op on every stamped pair `compose()` accepts.
//   * `stats.reservoir_size` IS axis `m`, so the same holds for two DECLARED reservoir caps that
//     differ. Its absent branch is a different matter and the arm above owns it — absence is not
//     a value the stamp can gate.
//   * `behavior.top_ngrams_size` IS NOT IN THE STAMP AT ALL. Neither is `ngram_size` nor
//     `max_ngram_keys`: the derivation names four axes and none of them is an n-gram parameter.
//     So two documents from the SAME production configuration line can differ here while carrying
//     byte-identical stamps, and the n-gram `min` in `compose.cpp` is the ONE declared-cap
//     `min` that is live between two stamped documents.
//
// WHY THIS IS NOT A CURIOSITY. The shipped pipeline stamps:
// `F-SRC-insight-eidos:insight_pipeline.cpp` sets `config.retention_profile =
// metalog::retention_profile_name(config)` on every run. So the reachability measured here is the
// reachability on the product's own path, not a property of hand-built fixtures. §12.2's
// commutativity MUST still wants `min` at all three sites — a conformant producer is not obliged
// to stamp anything, and `MetaLogConfig::retention_profile` DEFAULTS TO UNSET — but an argument
// that reads "the min protects the ordinary compose of two differently-capped documents" is true
// only where the stamp is absent, and that is worth stating before it is written into a standard.
//
// WHAT REDDENS EACH ARM, and they are four different edits:
//   A. adding `top_ngrams_size` to the stamp derivation      → arm 2's stamp-equality ASSERT.
//   B. removing `top_k_size` from the stamp derivation       → arm 1's throw expectation.
//   C. replacing the `min` at :592 with a pick-a-side        → arm 2's cap and its commutation.
//   D. replacing the `min` at :723 with a pick-a-side        → arm 3, the unstamped control.
// Arm 3 is what stops this test from reading as "the top-K min is dead code": it is not dead, it
// is reachable exactly where the stamp is absent, and that arm holds the positive boundary.

// One window of four distinct templates, driven so the bigram ring holds more than any cap below
// truncates to — otherwise a `min` could be asserted while no truncation ever bit.
void drive_ngram_window(meta::MetaLogEngine& engine)
{
    for (int rep = 0; rep < 12; ++rep)
    {
        engine.ingest_event(make_event("ngram alpha step"));
        engine.ingest_event(make_event("ngram beta step"));
        engine.ingest_event(make_event("ngram gamma step"));
        engine.ingest_event(make_event("ngram delta step"));
    }
}

TEST(ComposeAlgebraTest, TheRetentionStampDecidesWhichDeclaredCapMinIsReachable)
{
    constexpr std::size_t kNgramsWide{5};
    constexpr std::size_t kNgramsNarrow{2};
    constexpr std::size_t kTopKWide{6};
    constexpr std::size_t kTopKNarrow{3};

    const auto stamped{[](std::size_t top_k, std::size_t top_ngrams)
                       {
                           meta::MetaLogConfig cfg{
                               .top_k_size = top_k,
                               .reservoir_size = 0,
                               .top_ngrams_size = top_ngrams,
                               .emit_stability = false,
                               .max_param_histograms = 0,
                           };
                           cfg.retention_profile = meta::retention_profile_name(cfg);
                           return cfg;
                       }};

    const auto close_one{[](const meta::MetaLogConfig& cfg)
                         {
                             meta::MetaLogEngine engine{cfg};
                             engine.open_window(kT0);
                             drive_ngram_window(engine);
                             return engine.close_window(kT1);
                         }};

    // ── ARM 1. `top_k_size` is axis `k`, so two stamped documents that differ in it are REFUSED
    // at the §2.4 gate and the top-K `min` in `compose.cpp` never runs.
    const auto wide_k{stamped(kTopKWide, kNgramsWide)};
    const auto narrow_k{stamped(kTopKNarrow, kNgramsWide)};
    ASSERT_NE(wide_k.retention_profile, narrow_k.retention_profile)
        << "top_k_size must be an axis of the §2.4 stamp, or arm 1 is testing nothing:\n    wide:  "
           " "
        << wide_k.retention_profile.value_or("<unset>")
        << "\n    narrow: " << narrow_k.retention_profile.value_or("<unset>");

    const auto doc_wide_k{close_one(wide_k)};
    const auto doc_narrow_k{close_one(narrow_k)};
    ASSERT_EQ(doc_wide_k.retention_profile, wide_k.retention_profile)
        << "close_window must stamp the document from the config";

    EXPECT_THROW((void)meta::compose(doc_wide_k, doc_narrow_k), std::invalid_argument)
        << "two STAMPED documents differing only in top_k_size must be refused by the §2.4 gate "
           "BEFORE compose() reaches its top-K min — stamps were "
        << doc_wide_k.retention_profile.value_or("<unset>") << " vs "
        << doc_narrow_k.retention_profile.value_or("<unset>");
    EXPECT_THROW((void)meta::compose(doc_narrow_k, doc_wide_k), std::invalid_argument)
        << "and the refusal is symmetric — a gate that fires on one bracketing only would make "
           "§12.2's commutativity MUST depend on argument order";

    // ── ARM 2. `top_ngrams_size` is in NO axis, so the same two stamps compose and the `min` at
    // the n-gram min in `compose.cpp` is the one declared-cap min live between stamped documents.
    const auto wide_n{stamped(kTopKWide, kNgramsWide)};
    const auto narrow_n{stamped(kTopKWide, kNgramsNarrow)};
    ASSERT_EQ(wide_n.retention_profile, narrow_n.retention_profile)
        << "top_ngrams_size must NOT be an axis of the §2.4 stamp, or arm 2's premise is gone and "
           "the n-gram min is unreachable too:\n    wide:   "
        << wide_n.retention_profile.value_or("<unset>")
        << "\n    narrow: " << narrow_n.retention_profile.value_or("<unset>");

    const auto doc_wide_n{close_one(wide_n)};
    const auto doc_narrow_n{close_one(narrow_n)};
    ASSERT_TRUE(doc_wide_n.behavior.has_value() && doc_narrow_n.behavior.has_value())
        << "both inputs must carry a behavior block, or the third cap site is not on the path";
    ASSERT_EQ(doc_wide_n.behavior->top_ngrams_size, kNgramsWide);
    ASSERT_EQ(doc_narrow_n.behavior->top_ngrams_size, kNgramsNarrow);
    ASSERT_GT(doc_wide_n.behavior->top_ngrams.size(), kNgramsNarrow)
        << "the wide side must hold more entries than the narrow cap, or the truncation the min "
           "governs never bites and this arm asserts a number nothing enforces — held "
        << doc_wide_n.behavior->top_ngrams.size();

    const auto composed{meta::compose(doc_wide_n, doc_narrow_n)};
    const auto flipped{meta::compose(doc_narrow_n, doc_wide_n)};
    ASSERT_TRUE(composed.behavior.has_value() && flipped.behavior.has_value());
    EXPECT_EQ(composed.behavior->top_ngrams_size, kNgramsNarrow)
        << "the composed n-gram cap is the MINIMUM over what the inputs declared, not a side — "
           "got "
        << composed.behavior->top_ngrams_size << " from " << kNgramsWide << " and "
        << kNgramsNarrow;
    EXPECT_EQ(flipped.behavior->top_ngrams_size, composed.behavior->top_ngrams_size)
        << "and it is symmetric (§12.2 commutativity MUST) — picking a side would show up here "
           "and nowhere else";
    EXPECT_LE(composed.behavior->top_ngrams.size(), kNgramsNarrow)
        << "the array is re-truncated to the composed cap it declares (§8 clause 4), or the "
           "document declares a bound it does not honour — held "
        << composed.behavior->top_ngrams.size();

    // ── ARM 3. The positive boundary: UNSTAMPED, the top-K min is reachable and it is a min.
    // Without this arm, arm 1 would read as a claim that the top-K min is dead code.
    auto unstamped_wide{wide_k};
    auto unstamped_narrow{narrow_k};
    unstamped_wide.retention_profile.reset();
    unstamped_narrow.retention_profile.reset();

    const auto doc_unstamped_wide{close_one(unstamped_wide)};
    const auto doc_unstamped_narrow{close_one(unstamped_narrow)};
    ASSERT_FALSE(doc_unstamped_wide.retention_profile.has_value())
        << "MetaLogConfig::retention_profile defaults to unset and must stay clearable, or the "
           "gate has become unconditional and a conformant unstamped producer cannot compose";

    const auto unstamped_composed{meta::compose(doc_unstamped_wide, doc_unstamped_narrow)};
    EXPECT_EQ(unstamped_composed.stats.top_k_size, kTopKNarrow)
        << "with no stamp to gate them, two differing top-K caps DO reach the min at "
           "the top-K min in compose.cpp and it takes the smaller — got "
        << unstamped_composed.stats.top_k_size << " from " << kTopKWide << " and " << kTopKNarrow;
    EXPECT_EQ(meta::compose(doc_unstamped_narrow, doc_unstamped_wide).stats.top_k_size,
              unstamped_composed.stats.top_k_size)
        << "symmetric on the unstamped path too";
    EXPECT_FALSE(unstamped_composed.retention_profile.has_value())
        << "neither input stated a retention contract, so the composed document must not invent "
           "one (carry_processing_identifier)";
}

} // namespace
// NOLINTEND
