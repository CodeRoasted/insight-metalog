// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_compose_algebra.cpp — SPEC §12.2's three algebraic clauses over `compose()`, which had NO
// test anywhere in the reference implementation until this file (`rg associativ tests/` returned
// zero; DN-56.O3). §12.2 states three properties and they carry three different modal strengths,
// so they are three separate arms rather than one "algebra" test:
//
//   * Commutativity — **MUST**, over all required fields.  `compose(A,B)` vs `compose(B,A)`.
//   * Identity      — **MUST**.                            `compose(A, ZERO)` vs `A`.
//   * Associativity — **SHOULD** (best-effort).            `(A∘B)∘C` vs `A∘(B∘C)`.
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
// contains `top_ngrams_size`) — and `compose()` takes BOTH from `lhs`
// (`compose.cpp` `out.stats.top_k_size = lhs.stats.top_k_size`, and `merge_behavior`'s
// `lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size`). Picking one
// side is inherently non-commutative, so with mismatched producers the same PAIR yields two
// documents that disagree on a required field AND carry differently-truncated arrays.
//
// THIS ARM IS EXPECTED TO FAIL AGAINST THE IMPLEMENTATION AS IT STANDS. It is not a
// characterization of the current behaviour: it asserts the spec clause, and its red IS the
// finding (DN-56.D6). It goes green when the composed caps stop being taken from one side.
// Do not "repair" it by asserting the lhs-bias.
//
// WHAT THE ARM WOULD BE VACUOUS WITHOUT: the two preconditions below. If the two producers
// declared the SAME cap, or if the union were small enough that neither cut bit, both sides would
// agree by construction and the green would say nothing at all.
TEST(ComposeAlgebraTest, CommutativityHoldsOnTheRequiredCapFields)
{
    // Two producers over overlapping-but-different template alphabets, differing ONLY in the two
    // cap knobs. Same event budget on both sides so nothing but the caps can explain a divergence.
    const auto window{
        [](std::string_view prefix, std::size_t top_k, std::size_t top_ngrams)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{
                .top_k_size = top_k,
                .reservoir_size = 0,
                .top_ngrams_size = top_ngrams,
                .max_param_histograms = 0,
                .emit_stability = false,
            }};
            engine.open_window(kT0);
            // 12 distinct templates, each at a distinct frequency so the top-K
            // ranking is total and carries no ties.
            for (int t = 0; t < 12; ++t)
                for (int rep = 0; rep <= t; ++rep)
                    engine.ingest_event(make_event(std::string{prefix} + std::to_string(t)));
            return engine.close_window(kT1);
        }};

    const auto wide{window("wide template ", /*top_k=*/8, /*top_ngrams=*/8)};
    const auto narrow{window("narrow template ", /*top_k=*/3, /*top_ngrams=*/2)};

    // ── Preconditions: the discriminator is live. ──
    ASSERT_NE(wide.stats.top_k_size, narrow.stats.top_k_size)
        << "the two inputs must declare DIFFERENT top_k_size or this arm cannot separate a "
           "commutative composer from an lhs-biased one";
    ASSERT_TRUE(wide.behavior.has_value() && narrow.behavior.has_value());
    ASSERT_NE(wide.behavior->top_ngrams_size, narrow.behavior->top_ngrams_size)
        << "same requirement on the second cap";
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
        .max_param_histograms = 0,
        .emit_stability = false,
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

    // ── A SCOPED-OUT GAP, PINNED AS A POSITIVE BOUNDARY RATHER THAN LEFT SILENT. ──
    // §12.1 also says "C.stats.entropy_bits is recomputed from the merged counts", and `compose()`
    // never assigns that field — so identity fails on it too, by a DIFFERENT root than the caps
    // above and with a different owner. It is deliberately NOT asserted as identity here: this
    // lane was sent to pin §12.2's three clauses ahead of the composed-caps change, and folding an
    // unrelated unimplemented §12.1 clause into that arm would make a single red ambiguous about
    // which fix landed. The current state is pinned instead, so the gap cannot be lost and so the
    // day it IS implemented this site reds and points at the identity assertion it then owes.
    EXPECT_TRUE(doc_a.stats.entropy_bits.has_value())
        << "A must carry entropy_bits, or the boundary below is about an absent input";
    EXPECT_FALSE(composed.stats.entropy_bits.has_value())
        << "CHARACTERIZATION, not a requirement: §12.1 orders `C.stats.entropy_bits` recomputed "
           "from the merged counts and compose() does not compute it. If this reds, the clause "
           "was implemented — replace this boundary with the identity assertion "
           "`composed.stats.entropy_bits == doc_a.stats.entropy_bits` (A's tail is empty, so the "
           "merged counts ARE A's counts and the two must agree exactly).";
}

// ── §12.2 ASSOCIATIVITY (SHOULD) — a characterization of a DISCLOSED limitation ───────────────
//
// READ THIS BEFORE CHANGING EITHER ARM BELOW.
//
// These two arms assert that the composed reservoir is associative TODAY, and they are expected to
// go RED when the composed-document cap lands (DN-56.D2). That red is the design being applied,
// not a regression, and the correct response to it is to re-home these arms as the measured proof
// of §12.2's disclosure — NEVER to restore associativity by removing the cap.
//
// WHY IT YIELDS. Top-M selection under a FIXED total order is associative. The reservoir's order
// is not fixed: §12.1 mandates that salience be re-derived over the merged counts precisely
// because rarity shifts on merge, so the ranking KEY moves with the merge scope. Today nothing is
// ever dropped — `rederive_reservoir` admits every salience-positive candidate — so membership is
// the union of unions and is bracket-independent, and the final re-derivation runs at the same
// total scope either way. Introduce an admission bound and an entry cut at a low rung folds into
// `tail_count` and can never re-enter at the scale where it would have ranked.
//
// WHAT MAKES THESE ARMS NON-VACUOUS, and it is the first assertion in each: the scope-dependent
// ranking key is OBSERVED, not assumed. Each arm pins the exact salience both templates carry at a
// narrow rung and at the full rung, and shows the order between them changes. Without that, "the
// two bracketings agree" would be equally satisfied by a fixture where merge scope never moved a
// band — the green would be true and would say nothing about associativity. The exact values are
// the frozen band ladder written out (`src/stats/salience.cpp`: severity bands kBandOffPath 75,
// kBandError 80, kBandStrongOffPath 90, kBandFatal 100; rarity kRarityUncommon 90 at < 1 %,
// kRarityRare 100 at < 0.1 %), multiplied by hand — never a second call into salience_score().

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
    constexpr std::size_t kInputReservoirCap{1};

    [[nodiscard]] meta::MetaLogDocument make_a(const ReservoirSeed& salient)
    {
        return make_document("2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", 1202, kTopKSize,
                             kInputReservoirCap, {{"filler a one", 700}, {"filler a two", 500}},
                             {salient});
    }
    [[nodiscard]] meta::MetaLogDocument make_b(const ReservoirSeed& salient)
    {
        return make_document("2026-01-01T00:01:00Z", "2026-01-01T00:02:00Z", 1203, kTopKSize,
                             kInputReservoirCap, {{"filler b one", 700}, {"filler b two", 500}},
                             {salient});
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

// Band pair (kBandOffPath 75, kBandError 80) — DN-56.D3's STRICT flip.
// A benign off-path Info template outranks a rare error at the two-document scale and is outranked
// by it at the three-document scale, because widening the merge lifts the error's rarity band and
// nothing else. 75×100 = 7500 > 80×90 = 7200, then 80×100 = 8000 > 7500.
TEST(ComposeAlgebraTest, ComposedReservoirIsAssociativeAcrossTheStrictBandFlip)
{
    constexpr std::string_view kOffPath{"took alternate cache path"};
    constexpr std::string_view kError{"connection refused to db"};
    const auto off_path_id{insight::template_id_of(kOffPath)};
    const auto error_id{insight::template_id_of(kError)};

    const auto doc_a{make_a(ReservoirSeed{.tmpl = kOffPath,
                                          .count = 2,
                                          .structural_surprise = 75,
                                          .level = std::nullopt,
                                          .salience = 6750})}; // 75 × 90 at A's own 1202 lines
    const auto doc_b{make_b(ReservoirSeed{.tmpl = kError,
                                          .count = 3,
                                          .structural_surprise = 0,
                                          .level = insight::LogLevel::Error,
                                          .salience = 7200})}; // 80 × 90 at B's own 1203 lines
    const auto doc_c{make_c()};

    // ── ① THE MECHANISM IS LIVE: the ranking key moves with merge scope. ──
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.window.lines_observed, 2405U) << "the narrow rung's line total is the band input";
    const auto off_path_narrow{salience_at(ab, off_path_id, "the off-path template")};
    const auto error_narrow{salience_at(ab, error_id, "the error template")};
    EXPECT_EQ(off_path_narrow, 7500U)
        << "kBandOffPath 75 × kRarityRare 100 (2/2405 = 0.083 % < 0.1 %); got " << off_path_narrow
        << "\n    " << render_reservoir(ab);
    EXPECT_EQ(error_narrow, 7200U)
        << "kBandError 80 × kRarityUncommon 90 (3/2405 = 0.125 %, below 1 % but not below 0.1 %); "
           "got "
        << error_narrow << "\n    " << render_reservoir(ab);
    // NON-FATAL on purpose, here and at every ordering observation below. These are
    // preconditions about the RANKING KEY; the property this arm exists for is the associativity
    // comparison at ③. A fatal assert here would abort before ③ ever ran — which is exactly what
    // happens under an admission cap, and it would leave the load-bearing assertion unproven in
    // both directions (measured 2026-08-24: the first cap mutation reported ① only).
    EXPECT_GT(off_path_narrow, error_narrow)
        << "at the narrow rung the benign off-path template must OUTRANK the error, or the flip "
           "this arm characterizes never happens";

    const auto abc{meta::compose(ab, doc_c)};
    ASSERT_EQ(abc.window.lines_observed, 3605U) << "the full rung's line total is the band input";
    const auto off_path_wide{salience_at(abc, off_path_id, "the off-path template")};
    const auto error_wide{salience_at(abc, error_id, "the error template")};
    EXPECT_EQ(off_path_wide, 7500U) << "unchanged: 2/3605 was already below 0.1 %";
    EXPECT_EQ(error_wide, 8000U)
        << "kBandError 80 × kRarityRare 100 — 3/3605 = 0.083 % crossed the 0.1 % threshold; got "
        << error_wide << "\n    " << render_reservoir(abc);
    EXPECT_GT(error_wide, off_path_wide)
        << "at the full rung the order must INVERT. This is the whole reason a cap would break "
           "associativity: the reservoir's ranking key is scope-dependent by design (§12.1 "
           "re-derives salience over the merged counts), so a low-rung cut is not a cut the high "
           "rung would have made.";

    // ── ② THE CAP WOULD BITE HERE — the datum that makes the future red predictable. ──
    ASSERT_EQ(doc_a.stats.reservoir_size, kInputReservoirCap);
    ASSERT_EQ(doc_b.stats.reservoir_size, kInputReservoirCap);
    EXPECT_EQ(ab.stats.reservoir.size(), 2U)
        << "two documents each declaring M=" << kInputReservoirCap << " compose to a reservoir of "
        << ab.stats.reservoir.size()
        << " — the composed reservoir is bounded by the UNION, never by either input's M.\n    "
        << render_reservoir(ab);
    EXPECT_FALSE(ab.stats.reservoir_size.has_value())
        << "and the composed document declares no cap, so SPEC §8 clause 4 makes no claim about "
           "that array — which is why the conformance validator is vacuous on it.";

    // ── ③ ASSOCIATIVITY, TODAY. Expected RED once ② stops being true. ──
    const auto bc{meta::compose(doc_b, doc_c)};
    const auto a_bc{meta::compose(doc_a, bc)};
    EXPECT_EQ(abc.window.lines_observed, a_bc.window.lines_observed);
    EXPECT_EQ(abc.stats.unique_templates, a_bc.stats.unique_templates);
    EXPECT_EQ(reservoir_signature(abc), reservoir_signature(a_bc))
        << "SPEC §12.2 associativity (SHOULD) over the composed reservoir. It holds today because "
           "nothing is ever dropped: membership is the union of unions and the final salience is "
           "re-derived at a total scope both bracketings share.\n"
        << "    (A∘B)∘C: " << render_reservoir(abc) << "\n"
        << "    A∘(B∘C): " << render_reservoir(a_bc) << "\n"
        << "  IF THIS RED ARRIVED WITH A COMPOSED-DOCUMENT CAP, IT IS THE DESIGN, NOT A "
           "REGRESSION (DN-56.D3): the low rung cut an entry the high rung would have kept, and "
           "a cut entry folds into tail_count and cannot re-enter. Re-home this arm as the "
           "measured proof of §12.2's disclosure; do not remove the cap.";
}

// Band pair (kBandStrongOffPath 90, kBandFatal 100) — the flip that needs no band coincidence.
// The flip condition is 0.9·sev_Q < sev_P < sev_Q, and this pair sits exactly ON the lower edge:
// 0.9 × 100 = 90 is not strictly less than 90. So the narrow rung is an EXACT TIE (90×100 = 9000
// = 100×90) resolved by the meaning-blind `template_id` order, and the wide rung is a strict win
// for the fatal (100×100 = 10000). Note this is the opposite side from DN-56.D3's prose, which
// places the tie at the widened rung; the tie is at the NARROW rung, and the arithmetic is here.
TEST(ComposeAlgebraTest, ComposedReservoirIsAssociativeAcrossTheTieBreakBandPair)
{
    constexpr std::string_view kStrongOffPath{"took alternate cache path"};
    constexpr std::string_view kFatal{"connection refused to db"};
    const auto off_path_id{insight::template_id_of(kStrongOffPath)};
    const auto fatal_id{insight::template_id_of(kFatal)};

    const auto doc_a{make_a(ReservoirSeed{.tmpl = kStrongOffPath,
                                          .count = 2,
                                          .structural_surprise = 90,
                                          .level = std::nullopt,
                                          .salience = 8100})}; // 90 × 90 at A's own 1202 lines
    const auto doc_b{make_b(ReservoirSeed{.tmpl = kFatal,
                                          .count = 3,
                                          .structural_surprise = 0,
                                          .level = insight::LogLevel::Fatal,
                                          .salience = 9000})}; // 100 × 90 at B's own 1203 lines
    const auto doc_c{make_c()};

    // ── ① The tie at the narrow rung, and the strict win at the wide one. ──
    const auto ab{meta::compose(doc_a, doc_b)};
    const auto off_path_narrow{salience_at(ab, off_path_id, "the strong-off-path template")};
    const auto fatal_narrow{salience_at(ab, fatal_id, "the fatal template")};
    EXPECT_EQ(off_path_narrow, 9000U) << "kBandStrongOffPath 90 × kRarityRare 100";
    EXPECT_EQ(fatal_narrow, 9000U) << "kBandFatal 100 × kRarityUncommon 90";
    EXPECT_EQ(off_path_narrow, fatal_narrow)
        << "this pair's whole point is that the narrow rung is an EXACT salience tie, so the "
           "emitted order is decided by the meaning-blind template_id tie-break alone (§3.7.2.1)";
    // The tie-break is `template_id` ascending — a content hash, so which one leads carries no
    // meaning. Assert the ORDER the composer actually emits against the ids' own order, so a
    // tie-break that silently became insertion-ordered (i.e. argument-order-dependent, hence
    // non-commutative) reds here.
    EXPECT_EQ(ab.stats.reservoir.size(), 2U) << render_reservoir(ab);
    EXPECT_EQ(leading_reservoir_id(ab), std::optional{std::min(off_path_id, fatal_id)})
        << "a salience tie must resolve to the lower template_id, not to argument order.\n    "
        << render_reservoir(ab);

    const auto abc{meta::compose(ab, doc_c)};
    const auto off_path_wide{salience_at(abc, off_path_id, "the strong-off-path template")};
    const auto fatal_wide{salience_at(abc, fatal_id, "the fatal template")};
    EXPECT_EQ(off_path_wide, 9000U) << "unchanged: already below 0.1 % at the narrow rung";
    EXPECT_EQ(fatal_wide, 10000U) << "kBandFatal 100 × kRarityRare 100 — the ceiling of the ladder";
    EXPECT_GT(fatal_wide, off_path_wide)
        << "widening the merge must break the tie in the fatal's favour, or this arm is a "
           "duplicate of the strict-flip arm";
    EXPECT_EQ(leading_reservoir_id(abc), std::optional{fatal_id})
        << "and at the wide rung the ranking — not the tie-break — decides the order.\n    "
        << render_reservoir(abc);

    // ── ② ASSOCIATIVITY, TODAY. Same disclosure as the strict-flip arm above. ──
    const auto bc{meta::compose(doc_b, doc_c)};
    const auto a_bc{meta::compose(doc_a, bc)};
    EXPECT_EQ(reservoir_signature(abc), reservoir_signature(a_bc))
        << "SPEC §12.2 associativity over a reservoir whose narrow-rung order is decided by a "
           "meaning-blind tie-break. A cap of 1 at the narrow rung would keep whichever id sorts "
           "lower and drop the other — including, half the time, the FATAL.\n"
        << "    (A∘B)∘C: " << render_reservoir(abc) << "\n"
        << "    A∘(B∘C): " << render_reservoir(a_bc);
}

} // namespace
// NOLINTEND
