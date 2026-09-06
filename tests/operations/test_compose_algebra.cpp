
// refs: DN-56.O3, F-SRC-metalog-spec:SPEC.md
// invariant: SPEC 12.2's three clauses carry three different modal strengths and are therefore
// separate arms: commutativity MUST, identity MUST, associativity SHOULD.
// note: associativity is not uniformly SHOULD: behavior ordering MAY differ, its counts MUST agree.
// invariant: the associativity arms assert the exact scope-dependence DN-56.D3 rules, from BOTH
// sides; every magnitude is derived from the frozen band ladder and none is read off a run.
// note: homed as a unit test: compose() is a pure function of two documents, crossing no seam.
// invariant: the oracle is the algebra, never a second call into compose(); each relational arm
// also pins the exact magnitudes, so a relation holding by mutual collapse cannot pass.
// note: no RNG, no threads, no wall clock -- literal epoch time_points and literal ISO strings.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;

using Clock = std::chrono::system_clock;
constexpr Clock::time_point kT0{std::chrono::seconds{1700000000}};
constexpr Clock::time_point kT1{std::chrono::seconds{1700000060}};

// invariant: SPEC 3 orders top_k by count descending and 12.1 truncates it, so every signature
// here is a sequence and never a set.
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

// note: the reservoir as an ORDERED sequence: membership, merged count, and re-derived salience.
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

// refs: DN-56.D6, DN-56.D2
// invariant: SPEC 12.2 MUST -- compose(A,B) and compose(B,A) agree on all required fields, and
// stats.top_k_size and behavior.top_ngrams_size are both required by the published schema.
// invariant: it holds because the composed cap is min over the DECLARED caps and min is symmetric;
// a red means a cap site went back to picking a side, or a new capped block landed.
// note: never repair a red here by asserting a side-bias.
// pre: the preconditions below are what stop this arm being vacuous -- equal caps, or a union under
// the wider cap, would make both sides agree by construction.
TEST(ComposeAlgebraTest, CommutativityHoldsOnTheRequiredCapFields)
{
    // note: two producers over overlapping alphabets, differing ONLY in the cap knobs.
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
            // note: 12 templates at distinct frequencies, so the top-K ranking is total and untied.
            for (int t = 0; t < 12; ++t)
                for (int rep = 0; rep <= t; ++rep)
                    engine.ingest_event(make_event(std::string{prefix} + std::to_string(t)));
            return engine.close_window(kT1);
        }};

    const auto wide{window("wide template ", /*top_k=*/8, /*reservoir=*/8, /*top_ngrams=*/8)};
    const auto narrow{window("narrow template ", /*top_k=*/3, /*reservoir=*/2, /*top_ngrams=*/2)};

    // pre: the discriminator must be live at all three cap sites.
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

    // pre: the union must EXCEED the wider cap, or the wide cut never bites and the two sides could
    // agree for a reason that has nothing to do with commutativity.
    ASSERT_GT(ab.stats.unique_templates, wide.stats.top_k_size)
        << "the merged union (" << ab.stats.unique_templates << ") must exceed the wider cap ("
        << wide.stats.top_k_size << ") so BOTH truncations are live";

    // note: controls: what already holds, which is what makes the arms below about the CAP.
    EXPECT_EQ(ab.window.lines_observed, ba.window.lines_observed)
        << "lines_observed is a sum — commutative by construction";
    EXPECT_EQ(ab.stats.unique_templates, ba.stats.unique_templates)
        << "the union's cardinality does not depend on argument order";

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

    // invariant: stats.reservoir_size is OPTIONAL so 12.2's MUST does not reach it; a claim whose
    // value depends on argument order is a defect at any modal strength.
    EXPECT_EQ(ab.stats.reservoir_size, ba.stats.reservoir_size)
        << "the composed reservoir cap must not depend on argument order.\n"
        << "    compose(wide, narrow): " << render_reservoir(ab) << "\n"
        << "    compose(narrow, wide): " << render_reservoir(ba);
    EXPECT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{2U})
        << "and it is the MINIMUM over the declared caps (8 and 2), not either input's own — a "
           "merge is never finer than its coarsest member. Got "
        << (ab.stats.reservoir_size ? std::to_string(*ab.stats.reservoir_size)
                                    : std::string{"<absent>"});

    // invariant: the composed caps are pinned as VALUES, so a composer made commutative by taking
    // the MAXIMUM -- equally symmetric, and wrong -- reds here rather than passing.
    EXPECT_EQ(ab.stats.top_k_size, narrow.stats.top_k_size)
        << "min(8, 3) = 3; got " << ab.stats.top_k_size;
    EXPECT_EQ(ab.behavior->top_ngrams_size, narrow.behavior->top_ngrams_size)
        << "min(8, 2) = 2; got " << ab.behavior->top_ngrams_size;
}

// refs: DN-56.O3
// invariant: SPEC 12.2 MUST -- compose(A, ZERO) equals A. Literal document equality is refused by
// 12.1, which orders provenance and coordinate constructed, so those two blocks are excluded.
// invariant: identity is asserted over every required field plus every optional field A declares.
// pre: A's tail is EMPTY, asserted before anything else: SPEC 12.3 makes composition lossy on a
// tailed input, so an identity red over a tailed document would be 12.3 working, not a defect.
// invariant: stats.reservoir_size is the load-bearing field -- ZERO declares no cap, and only
// identity separates a min that reads absence as a value from one that skips it.
TEST(ComposeAlgebraTest, IdentityPreservesTheDocumentIncludingItsDeclaredReservoirCap)
{
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

    // note: ZERO -- same producer, same contract identifiers, same reported window, and no events.
    meta::MetaLogEngine engine_z{cfg};
    engine_z.open_window(kT0);
    const auto zero{engine_z.close_window(kT1)};

    // pre: A is the fixture the scope above describes.
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

    // pre: ZERO is genuinely a zero, and it declares NO cap.
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

    EXPECT_EQ(reservoir_signature(composed), reservoir_signature(doc_a))
        << "the re-derivation is over A's own counts and A's own line total, so it must land on "
           "A's own salience.\n    A:        "
        << render_reservoir(doc_a) << "\n    composed: " << render_reservoir(composed);

    // refs: DN-56.O3
    // note: entropy_bits has its own arm below, so one red is never ambiguous between two clauses.
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
}

// refs: DN-56.D2, DN-56.D3
// invariant: the composed reservoir is NOT associative by design; these arms assert the exact
// scope-dependence DN-56.D3 rules and the equality it rules where no cap binds.
// invariant: top-M selection is associative only under a FIXED total order, and SPEC 12.1
// re-derives salience over the merged counts, so the ranking key moves with merge scope.
// note: bands 75 off-path, 80 error, 90 strong-off-path, 100 fatal; rarity 90 uncommon, 100 rare.
// invariant: under an admission bound an entry cut at a low rung folds into tail_count and can
// never re-enter at the scale where it would have ranked.
// note: the divergence also reaches the REQUIRED field stats.unique_templates, which DN-56 omits.
// invariant: salience is severity times rarity, so a flip on widening needs 0.9*sev_Q < sev_P <
// sev_Q.
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
        std::uint32_t salience;
    };

    // invariant: hand-built inputs on purpose -- compose()'s domain is two documents, so building
    // them directly puts the band pair and the line totals exactly where the arithmetic needs them.
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

    // invariant: A.B at 2405 lines puts 3/2405 at uncommon 90 and A.B.C at 3605 puts it at rare
    // 100, while the count-2 entry is under 0.1% at both rungs and stays at 100.
    // note: top_k_size 4 keeps both salient entries below the composed top-K at every rung.
    constexpr std::size_t kTopKSize{4};
    // invariant: the cap is the VARIABLE of this section -- the same three documents compose at a
    // cap that binds (1, one slot for two salient templates) and at one that cannot (8).
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
    // note: C carries no salient template and declares no cap: it is pure merge scope.
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

    // note: an optional, not front(): an arm reading the leader must red on an empty reservoir.
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

// invariant: the boundary is asserted from BOTH sides -- without this arm a divergence would be
// equally satisfied by a lost entry, a re-derivation at the wrong scope or an ordered tie-break.
// invariant: it also carries the mechanism: at the narrow rung the off-path outranks the error
// (75x100 = 7500 over 80x90 = 7200) and at the full rung the order inverts (80x100 = 8000).
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
                                          .salience = 6750},
                            kSlackCap)};
    const auto doc_b{make_b(ReservoirSeed{.tmpl = kError,
                                          .count = 3,
                                          .structural_surprise = 0,
                                          .level = insight::LogLevel::Error,
                                          .salience = 7200},
                            kSlackCap)};
    const auto doc_c{make_c()};

    // pre: the cap cannot bind here -- more slots than candidates, asserted rather than assumed.
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.window.lines_observed, 2405U) << "the narrow rung's line total is the band input";
    ASSERT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{kSlackCap})
        << "min(8, 8) = 8 — the composed document declares its own cap (DN-56.D2)";
    ASSERT_EQ(ab.stats.reservoir.size(), 2U)
        << "both salient templates must survive, or this arm proves nothing about a cap that does "
           "not bind.\n    "
        << render_reservoir(ab);

    // note: the mechanism -- the ranking key moves with merge scope, and the order inverts.
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

// refs: DN-56.D3
// invariant: the band pair 75/80 is DN-56.D3's STRICT flip, ratio 1.067, inside the flip window;
// the only change from the arm above is that A and B declare a reservoir cap of 1.
// invariant: A.B at 2405 keeps the off-path (7500 over 7200) and B.C at 2403 keeps the error at
// 7200, while A.(B.C) at 3605 inverts to the error at 8000.
// invariant: so the two bracketings emit different templates at different counts and saliences, and
// disagree on the REQUIRED field stats.unique_templates; any other divergence reds here.
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

    // pre: the composed document declares its own cap, and that cap BINDS.
    const auto ab{meta::compose(doc_a, doc_b)};
    ASSERT_EQ(ab.window.lines_observed, 2405U);
    EXPECT_EQ(ab.stats.reservoir_size, std::optional<std::size_t>{kBindingCap})
        << "min(1, 1) = 1 — DN-56.D2: a composed document declares its own cap, so SPEC §8 "
           "clause 4 now makes a checkable claim about the array (it made none before).\n    "
        << render_reservoir(ab);
    ASSERT_EQ(ab.stats.reservoir.size(), 1U)
        << "two salient candidates, one slot — the cut is what the rest of this arm is about.\n    "
        << render_reservoir(ab);

    // note: which entry the cut keeps, and where the other one goes.
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

    // invariant: the cut reaches a REQUIRED field too: DN-56.D3 argues what yields is a property of
    // an OPTIONAL block, so the disclosure owed is wider than it states.
    EXPECT_EQ(abc.stats.unique_templates, 7U)
        << "6 fillers + the off-path; the error is not in the union because A∘B dropped it. Got "
        << abc.stats.unique_templates;
    EXPECT_EQ(a_bc.stats.unique_templates, 8U)
        << "6 fillers + the off-path + the error, which B∘C kept. Got "
        << a_bc.stats.unique_templates;
    EXPECT_NE(abc.stats.unique_templates, a_bc.stats.unique_templates)
        << "the SAME three documents, bracketed two ways, disagree on a REQUIRED field";
}

// refs: DN-56.D3
// invariant: the band pair 90/100 sits exactly ON the flip window's lower edge, so the narrow rung
// is an EXACT tie (90x100 = 9000 = 100x90) and the wide rung a strict win for the fatal (10000).
// note: DN-56.D3's prose places the tie at the WIDENED rung; the arithmetic puts it at the NARROW.
// invariant: under a cap of 1 an exact tie allocates the single slot by template_id ascending, a
// content hash that is deterministic and MEANING-BLIND.
// invariant: with today's ids the tie-break keeps the FATAL and the two bracketings agree, so
// associativity survives this pair by luck and not by property.
// note: the id order is pinned below, so a change to canon's hash forces a re-derivation here.
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
                                      .salience = 8100};
    const ReservoirSeed fatal_seed{.tmpl = kFatal,
                                   .count = 3,
                                   .structural_surprise = 0,
                                   .level = insight::LogLevel::Fatal,
                                   .salience = 9000};
    const auto doc_c{make_c()};

    // pre: the tie is observed where BOTH entries survive -- a cap of 1 emits only one of them, so
    // the same pair is composed at a slack cap first.
    const auto slack{
        meta::compose(make_a(off_path_seed, kSlackCap), make_b(fatal_seed, kSlackCap))};
    ASSERT_EQ(slack.window.lines_observed, 2405U);
    ASSERT_EQ(slack.stats.reservoir.size(), 2U) << render_reservoir(slack);
    EXPECT_EQ(salience_at(slack, off_path_id, "the strong-off-path template"), 9000U)
        << "kBandStrongOffPath 90 × kRarityRare 100 (2·1000 = 2000 < 2405)";
    EXPECT_EQ(salience_at(slack, fatal_id, "the fatal template"), 9000U)
        << "kBandFatal 100 × kRarityUncommon 90 (3·1000 = 3000 > 2405, 3·100 = 300 < 2405)";

    // note: under the cap, the tie-break allocates the single slot.
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

    // note: the order of two content hashes, pinned as the FACT the derivation rests on.
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

    // note: the consequence for associativity, derived from the tie-break and not from the design.
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

    // note: the cost is still paid on a REQUIRED field, whichever entry the hash kept.
    EXPECT_EQ(abc.stats.unique_templates, 7U)
        << "6 fillers + the fatal; the off-path left the document at the narrow rung. Got "
        << abc.stats.unique_templates;
    EXPECT_EQ(a_bc.stats.unique_templates, 8U)
        << "6 fillers + the fatal + the off-path, which never met a binding cut on this side. Got "
        << a_bc.stats.unique_templates;
}

// refs: DN-56.D5, F-SRC-metalog-spec:GOVERNANCE.md
// invariant: SPEC 12.1 states entropy_bits is recomputed from the merged counts and compose() never
// assigned it, so GOVERNANCE 3 rules the implementation buggy and the spec text stands.
// note: kept out of the identity arm so one red is never ambiguous between two clauses.
// invariant: the oracle is closed-form arithmetic -- every count and line total is a power of two,
// so det_log2_fixed is exact and the whole reduction is an exact integer divide.
TEST(ComposeAlgebraTest, ComposedEntropyBitsIsRecomputedFromTheMergedCounts)
{
    // invariant: entropy is over the MERGED counts and not the retained top_k -- four templates at
    // 64 over 256 lines is uniform over four, exactly two bits, though only two are published.
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

    // invariant: SPEC 12.3 makes composition lossy on a tailed input, so the lost mass enters as
    // ONE residual bucket: dropping it over-states concentration and attributing it invents a fact.
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

    // invariant: the identity leg -- A's tail is empty so the merged counts ARE A's counts, and the
    // equality is exact because the accumulation is integer and order-independent.
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

    // invariant: an event-free composition has no distribution to describe, so the field is omitted
    // rather than emitted as a 0.0 that would read as perfectly concentrated.
    const auto empty{meta::compose(zero, zero)};
    ASSERT_EQ(empty.window.lines_observed, 0U);
    EXPECT_FALSE(empty.stats.entropy_bits.has_value())
        << "zero lines is an absent distribution, not an entropy of zero";
}

// refs: DN-56.D8, DN-57.O1
// invariant: the RFC asserts to external implementers that 12.2's associativity is violated BY THE
// FORMAT, so the falsifier must reach the property with the reservoir mechanism out of the picture.
// invariant: these documents declare no reservoir cap and carry no reservoir entry, so every line
// of rederive_reservoir is inert and the ONE remaining lossy operator is top_k_size truncation.
// note: the four counts 10/9/8/1 are distinct, so no template_id tie-break decides anything here.
// invariant: a cut entry leaves the document ENTIRELY and only its mass survives in tail_count, so
// mass is associative and a SET is not, and stats.unique_templates is REQUIRED.
// note: unique_templates 3 vs 4 and tail_unique 1 vs 2, while lines_observed and tail_count AGREE.
// invariant: that agreement is the load-bearing half -- no mass is lost or invented, so the
// divergence is a loss of IDENTITY and cannot be dismissed as a counting bug.
TEST(ComposeAlgebraTest, TopKTruncationAloneBreaksAssociativityWithNoReservoirAnywhere)
{
    constexpr std::size_t kCut{2};
    const auto a{make_document("2026-03-01T00:00:00Z", "2026-03-01T00:01:00Z", 11, kCut,
                               std::nullopt, {{"assoc alpha", 10}, {"assoc beta", 1}}, {})};
    const auto b{make_document("2026-03-01T00:01:00Z", "2026-03-01T00:02:00Z", 9, kCut,
                               std::nullopt, {{"assoc gamma", 9}}, {})};
    const auto c{make_document("2026-03-01T00:02:00Z", "2026-03-01T00:03:00Z", 8, kCut,
                               std::nullopt, {{"assoc delta", 8}}, {})};

    // pre: the mechanism assertion comes first -- if the reservoir machinery were live on these
    // inputs, nothing below would be a statement about top_k truncation.
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

    // note: the two totals AGREE, so no mass is lost or invented by either bracketing.
    EXPECT_EQ(ab_c.window.lines_observed, 28U);
    EXPECT_EQ(a_bc.window.lines_observed, 28U) << "11 + 9 + 8 either way";
    EXPECT_EQ(ab_c.stats.tail_count, 9U)
        << "8 (d) + 1 (AB's lumped b). Got " << ab_c.stats.tail_count;
    EXPECT_EQ(a_bc.stats.tail_count, 9U)
        << "8 (d) + 1 (b, still a template until this cut). Got " << a_bc.stats.tail_count;

    // note: and the SETS do not.
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

// invariant: the SAME three documents at a top_k_size that cannot cut are perfectly associative, so
// the divergence above is caused by the truncation and by nothing else in compose().
// note: both bracketings end at the full merged distribution over 28 lines, with an empty tail.
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

// refs: DN-56.D2, DN-56.O3
// invariant: DN-56.D2 argued the stamp makes the two caps equal in every normal case; that is FALSE
// -- build_reservoir returns early when every template fit in top-K, declaring no cap.
// invariant: a stamp gates VALUES and never PRESENCE, so an ordinary run mixes declared-cap and
// absent-cap documents constantly and that pair is the ordinary path, not an edge.
// invariant: a min folding absence in as zero would make the composed cap zero and silently drop
// every salient template of a busy window on contact with a quiet neighbour.
// pre: both inputs come from one real engine at one config, because a hand-built document could
// assert the min but not the premise, and the premise is the finding.
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

    // note: the QUIET window: all three templates fit in top-K, so build_reservoir returns early.
    meta::MetaLogEngine quiet_engine{cfg};
    quiet_engine.open_window(kT0);
    for (int rep = 0; rep < 20; ++rep)
    {
        quiet_engine.ingest_event(make_event("quiet alpha steady"));
        quiet_engine.ingest_event(make_event("quiet beta steady"));
        quiet_engine.ingest_event(make_event("quiet gamma steady"));
    }
    const auto quiet{quiet_engine.close_window(kT1)};

    // note: the BUSY window: two rare errors fall below top-K, so the cap IS declared.
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

    // pre: the premise, asserted at the producer: if either of these moves the finding has changed.
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

    // note: absent is SKIPPED, not read as zero, in both bracketings.
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

// refs: DN-56.D2, F-SRC-insight-eidos:insight_pipeline.cpp
// invariant: when both inputs are STAMPED, compose() never reaches the min at two of the three cap
// sites: top_k_size is axis k and reservoir_size is axis m, so a difference throws at the gate.
// invariant: behavior.top_ngrams_size is in NO stamp axis, so it is the ONE declared-cap min that
// is live between two stamped documents.
// note: the shipped pipeline stamps every run, so this reachability is the product's own path.
// invariant: 12.2's commutativity MUST still wants min at all three sites, a producer being under
// no obligation to stamp and retention_profile defaulting to unset.
// note: the window below drives four templates so the bigram ring exceeds every cap asserted.
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

// note: arm 1 pins the throw, arm 2 the live n-gram min, arm 3 the unstamped top-K min.
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

    // note: arm 1 -- top_k_size is axis k, so two stamped documents differing in it are refused.
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

    // note: arm 2: top_ngrams_size is in no axis, so the two stamps compose and the min runs.
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

    // note: arm 3 -- the positive boundary: unstamped, the top-K min is reachable and it is a min.
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
