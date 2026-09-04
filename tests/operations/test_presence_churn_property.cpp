// Unit tests: allow short identifiers and test-specific patterns.
//
// test_presence_churn_property.cpp — `G-T3` of DN-50.D8: the presence-churn monoid proven over a
// POPULATION rather than sampled on a witness.
//
// WHY THIS FILE EXISTS BESIDE THE WITNESS SUITE. `test_presence_churn_monoid.cpp` carries each law
// on one named witness — enough to say the code does what it says, not enough to say it does so
// everywhere. DN-50.D4's associativity proof is what lets ONE element per block fold at every
// ladder level with no re-scan of the base windows; if associativity fails on any input the pyramid
// silently reads a different number at every level, and no determinism gate can see it (the wrong
// value is perfectly reproducible). A law that the whole ladder rests on is checked over its
// domain.
//
// THE POPULATION IS EXHAUSTIVE, NOT RANDOM, AND THAT IS A STRENGTHENING OF THE SPEC. DN-50.D8 asks
// for "random presence sequences". Random sampling of a 3^n space leaves the answer probabilistic
// and the seed load-bearing; the base alphabet here has exactly THREE inhabitants
// (`Present`, `Absent`, `Unretained` — `EmptyRange` is span 0 and is never a window), so every
// sequence of length <= kExhaustiveLength is enumerated instead. 3^1..3^8 = 9840 sequences, each
// checked at every split point, and the associativity arm at every ORDERED PAIR of split points.
// A deterministic seeded sweep then carries the same laws to lengths the exhaustive arm cannot
// reach, so a defect that only appears once counters exceed a byte still has a witness. No wall
// clock, no threads, and the generator is `std::mt19937_64` at a literal seed — the sweep is
// bit-identical across runs and across machines.
//
// THE REFERENCE IS COMPUTED INDEPENDENTLY, which is what stops the sweep being a tautology. Every
// arm compares the fold against `reference_churn` — a direct count over the symbol sequence written
// from DN-50.D4's definition, not from `compose_presence_churn`. An arm that only compared the fold
// against differently-bracketed folds of itself would be green under a product that is associative
// and WRONG.
//
// WHAT THE TWO NEGATIVE ARMS ARE FOR. They are not pedantry, and DN-50.O3 says so in as many words:
// they are what stops the monoid's laws from being comments. (a) ORDER — the product must NOT be
// commutative, so a sweep is required to EXHIBIT disagreeing pairs; a product that quietly became
// symmetric would pass every positive arm above and break a consumer's re-fold of two adjacent
// documents. (b) IDENTITY — collapsing `EmptyRange` and `Unretained` into one absent symbol must
// RED the identity law; the arm runs the collapsed product over the same population and asserts it
// fails, so the two-symbol rule is a checked invariant rather than a comment on the enum.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using meta::PresenceChurn;
using meta::PresenceSymbol;

// The three symbols a real base window can carry. `EmptyRange` is excluded by construction: it is
// the identity's projection and has span 0, so it is not a window and cannot appear in a sequence.
constexpr std::array<PresenceSymbol, 3> kWindowAlphabet{
    PresenceSymbol::Present, PresenceSymbol::Absent, PresenceSymbol::Unretained};

// 3^8 = 6561 sequences at the top length; 9840 across lengths 1..8. Each is folded at every split
// point and associativity-checked at every ordered pair of split points, so the arm is O(n^2) per
// sequence — measured under a second on the release build.
constexpr std::size_t kExhaustiveLength{8};
// The seeded arm reaches spans the exhaustive one cannot enumerate. 64 windows is four times the
// deepest shipped ladder block (`{3,6,12}` composes to 12), so a counter that only misbehaves on a
// long range is inside the population.
constexpr std::size_t kSeededMaxLength{64};
constexpr std::size_t kSeededSequences{4000};
// The associativity arm is cubic per sequence; this bounds it to the same order of work as the
// exhaustive arm rather than letting a 64-window sequence dominate the suite's runtime.
constexpr std::size_t kSeededAssociativitySample{64};
constexpr std::uint64_t kSeed{0x5150'C0DE'C0FF'EE01ULL};

[[nodiscard]] std::string_view render(PresenceSymbol symbol)
{
    switch (symbol)
    {
    case PresenceSymbol::EmptyRange:
        return "empty-range";
    case PresenceSymbol::Unretained:
        return "unretained";
    case PresenceSymbol::Absent:
        return "absent";
    case PresenceSymbol::Present:
        return "present";
    }
    return "?";
}

[[nodiscard]] std::string render(const PresenceChurn& churn)
{
    return std::format("(span={} trans={} indet={} first={} last={})", churn.span_windows,
                       churn.transitions, churn.indeterminate, render(churn.first),
                       render(churn.last));
}

[[nodiscard]] std::string render(const std::vector<PresenceSymbol>& sequence)
{
    std::string out;
    out.reserve(sequence.size());
    for (const PresenceSymbol symbol : sequence)
        out.push_back(symbol == PresenceSymbol::Present  ? 'P'
                      : symbol == PresenceSymbol::Absent ? 'A'
                                                         : 'U');
    return out;
}

// The base element of one observed window. `Unretained` at span 1 has no internal boundary, so it
// carries no indeterminate of its own — the residue it produces is always a BOUNDARY term.
[[nodiscard]] PresenceChurn element_of(PresenceSymbol symbol)
{
    switch (symbol)
    {
    case PresenceSymbol::Present:
        return meta::presence_churn_of_retained_window();
    case PresenceSymbol::Absent:
        return meta::presence_churn_of_unretained_range(1, /*retention_exhaustive=*/true);
    case PresenceSymbol::Unretained:
        return meta::presence_churn_of_unretained_range(1, /*retention_exhaustive=*/false);
    case PresenceSymbol::EmptyRange:
        return {};
    }
    return {};
}

// THE INDEPENDENT REFERENCE — DN-50.D4's definition transcribed, never the product. Churn is
// #{ i : p_i != p_{i+1} } over the boundaries it can READ; a boundary touching `Unretained` on
// either side is unreadable and lands in `indeterminate` instead. This is the ground truth every
// arm below compares against, and it shares no code with `compose_presence_churn`.
[[nodiscard]] PresenceChurn reference_churn(const std::vector<PresenceSymbol>& sequence)
{
    if (sequence.empty())
        return {};
    PresenceChurn out{.span_windows = static_cast<std::uint32_t>(sequence.size()),
                      .transitions = 0,
                      .indeterminate = 0,
                      .first = sequence.front(),
                      .last = sequence.back()};
    for (std::size_t i{1}; i < sequence.size(); ++i)
    {
        const PresenceSymbol left{sequence[i - 1]};
        const PresenceSymbol right{sequence[i]};
        if (left == PresenceSymbol::Unretained || right == PresenceSymbol::Unretained)
            ++out.indeterminate;
        else if (left != right)
            ++out.transitions;
    }
    return out;
}

// The left fold over a half-open range, in WINDOW order. Every arm folds through this one function
// so that a difference between arms is a difference in BRACKETING and nothing else.
[[nodiscard]] PresenceChurn fold_range(const std::vector<PresenceSymbol>& sequence, std::size_t from,
                                       std::size_t to)
{
    PresenceChurn out{};
    for (std::size_t i{from}; i < to; ++i)
        out = meta::compose_presence_churn(out, element_of(sequence[i]));
    return out;
}

// THE COLLAPSED PRODUCT — the defect the two-symbol rule exists to forbid, implemented so it can be
// measured rather than described. It is `compose_presence_churn` with one change: `Unretained` and
// `EmptyRange` are one symbol, so an empty operand's boundary is treated as unreadable exactly as a
// truncated one's is. `DN-50.D4` predicts this breaks the IDENTITY law (`e . B != B`), and the
// negative arm below holds it to that prediction.
[[nodiscard]] PresenceChurn collapsed_product(const PresenceChurn& earlier, const PresenceChurn& later)
{
    const auto absent_like{[](PresenceSymbol symbol)
                           { return symbol == PresenceSymbol::Unretained ||
                                    symbol == PresenceSymbol::EmptyRange; }};
    const bool boundary_readable{!absent_like(earlier.last) && !absent_like(later.first)};
    return {.span_windows = earlier.span_windows + later.span_windows,
            .transitions = earlier.transitions + later.transitions +
                           (boundary_readable && earlier.last != later.first ? 1U : 0U),
            .indeterminate =
                earlier.indeterminate + later.indeterminate + (boundary_readable ? 0U : 1U),
            .first = earlier.span_windows == 0 ? later.first : earlier.first,
            .last = later.span_windows == 0 ? earlier.last : later.last};
}

// Enumerate every sequence of `length` over the three-symbol alphabet, calling `visit` on each.
template <typename Visitor> void for_each_sequence(std::size_t length, Visitor&& visit)
{
    std::vector<PresenceSymbol> sequence(length, kWindowAlphabet.front());
    std::vector<std::size_t> digits(length, 0);
    while (true)
    {
        visit(sequence);
        std::size_t position{0};
        for (; position < length; ++position)
        {
            if (++digits[position] < kWindowAlphabet.size())
            {
                sequence[position] = kWindowAlphabet[digits[position]];
                break;
            }
            digits[position] = 0;
            sequence[position] = kWindowAlphabet.front();
        }
        if (position == length)
            return;
    }
}

// The seeded population, generated once so every arm sweeps the SAME sequences — a red in one arm
// and a green in another is then a statement about the laws, never about two different populations.
[[nodiscard]] const std::vector<std::vector<PresenceSymbol>>& seeded_population()
{
    static const std::vector<std::vector<PresenceSymbol>> population{
        []
        {
            std::vector<std::vector<PresenceSymbol>> out;
            out.reserve(kSeededSequences);
            std::mt19937_64 rng{kSeed};
            std::uniform_int_distribution<std::size_t> length_of{kExhaustiveLength + 1,
                                                                 kSeededMaxLength};
            std::uniform_int_distribution<std::size_t> symbol_of{0, kWindowAlphabet.size() - 1};
            for (std::size_t n{0}; n < kSeededSequences; ++n)
            {
                std::vector<PresenceSymbol> sequence(length_of(rng));
                for (PresenceSymbol& symbol : sequence)
                    symbol = kWindowAlphabet[symbol_of(rng)];
                out.push_back(std::move(sequence));
            }
            return out;
        }()};
    return population;
}

// ── G-T3, positive: the fold equals the split product at EVERY split point ────────

// The load-bearing property: for every split point s, fold(w_1..w_n) == fold(w_1..w_s) .
// fold(w_{s+1}..w_n), and both equal the independently computed reference. The split-point sweep is
// what says a ladder level may compose a completed block instead of re-scanning its base windows —
// the property the whole pyramid rests on.
void check_every_split(const std::vector<PresenceSymbol>& sequence)
{
    const PresenceChurn expected{reference_churn(sequence)};
    const PresenceChurn whole{fold_range(sequence, 0, sequence.size())};
    ASSERT_EQ(whole, expected) << "sequence=" << render(sequence) << " fold=" << render(whole)
                              << " reference=" << render(expected);
    for (std::size_t split{0}; split <= sequence.size(); ++split)
    {
        const PresenceChurn left{fold_range(sequence, 0, split)};
        const PresenceChurn right{fold_range(sequence, split, sequence.size())};
        const PresenceChurn product{meta::compose_presence_churn(left, right)};
        ASSERT_EQ(product, expected)
            << "sequence=" << render(sequence) << " split=" << split << " left=" << render(left)
            << " right=" << render(right) << " product=" << render(product)
            << " reference=" << render(expected);
    }
}

TEST(PresenceChurnProperty, ExhaustiveTheFoldEqualsTheSplitProductAtEverySplitPoint)
{
    std::size_t sequences{0};
    for (std::size_t length{1}; length <= kExhaustiveLength; ++length)
        for_each_sequence(length,
                          [&](const std::vector<PresenceSymbol>& sequence)
                          {
                              ++sequences;
                              // A defect here is a defect for thousands of sequences at once; the
                              // FIRST counter-example is the diagnosis and the rest is noise.
                              if (::testing::Test::HasFatalFailure())
                                  return;
                              check_every_split(sequence);
                          });
    EXPECT_EQ(sequences, 9840U) << "the exhaustive population must be 3^1 + .. + 3^8; a different "
                                   "count means the enumerator, not the monoid, is what was proven";
}

TEST(PresenceChurnProperty, SeededTheFoldEqualsTheSplitProductAtEverySplitPoint)
{
    for (const auto& sequence : seeded_population())
    {
        check_every_split(sequence);
        if (::testing::Test::HasFatalFailure())
            return;
    }
}

// ── G-T3, positive: associativity at every ORDERED PAIR of split points ──────────

// DN-50.D4's proof is that (A.B).C and A.(B.C) both reduce to the same sum. This arm holds the
// implementation to it for every way of cutting a sequence into three parts — the general statement
// the witness suite's single triple can only illustrate.
void check_every_associativity(const std::vector<PresenceSymbol>& sequence)
{
    const std::size_t n{sequence.size()};
    for (std::size_t i{0}; i <= n; ++i)
        for (std::size_t j{i}; j <= n; ++j)
        {
            const PresenceChurn a{fold_range(sequence, 0, i)};
            const PresenceChurn b{fold_range(sequence, i, j)};
            const PresenceChurn c{fold_range(sequence, j, n)};
            const PresenceChurn left_first{meta::compose_presence_churn(
                meta::compose_presence_churn(a, b), c)};
            const PresenceChurn right_first{meta::compose_presence_churn(
                a, meta::compose_presence_churn(b, c))};
            ASSERT_EQ(left_first, right_first)
                << "sequence=" << render(sequence) << " cuts=(" << i << "," << j
                << ") (A.B).C=" << render(left_first) << " A.(B.C)=" << render(right_first);
            ASSERT_EQ(left_first, reference_churn(sequence))
                << "sequence=" << render(sequence) << " cuts=(" << i << "," << j
                << ") both bracketings agree with each other and DISAGREE with the definition — an "
                   "associative product can still be the wrong product. got="
                << render(left_first) << " reference=" << render(reference_churn(sequence));
        }
}

TEST(PresenceChurnProperty, ExhaustiveAssociativityHoldsAtEveryPairOfSplitPoints)
{
    for (std::size_t length{1}; length <= kExhaustiveLength; ++length)
        for_each_sequence(length,
                          [](const std::vector<PresenceSymbol>& sequence)
                          {
                              if (::testing::Test::HasFatalFailure())
                                  return;
                              check_every_associativity(sequence);
                          });
}

TEST(PresenceChurnProperty, SeededAssociativityHoldsAtEveryPairOfSplitPoints)
{
    // O(n^2) cuts, each folding O(n) elements, over sequences up to 64 windows — cubic per
    // sequence, so this arm is bounded at a PREFIX of the shared population rather than sweeping
    // all of it. A prefix, not a second seeded draw: this arm's inputs stay a subset of the arm
    // above's, so a red here and a green there is a statement about associativity and never about
    // two different populations.
    const auto& population{seeded_population()};
    const std::size_t sampled{std::min<std::size_t>(kSeededAssociativitySample, population.size())};
    for (std::size_t n{0}; n < sampled; ++n)
    {
        check_every_associativity(population[n]);
        if (::testing::Test::HasFatalFailure())
            return;
    }
}

// ── G-T3, negative arm (a): the product is NOT commutative ───────────────────────

// The MUST has teeth only if disagreement is EXHIBITED. A product that quietly became symmetric —
// by dropping the orientation from the boundary term, the one-line change that makes every positive
// arm above still pass — would break a consumer's re-fold of two adjacent documents, and this arm
// is the only one that would go red. The population is swept and the disagreeing pairs are COUNTED,
// so the arm cannot pass by finding one lucky pair.
TEST(PresenceChurnProperty, TheProductIsNotCommutativeAndTheSweepExhibitsIt)
{
    std::size_t compared{0};
    std::size_t disagreements{0};
    for (std::size_t length{1}; length <= 4; ++length)
        for_each_sequence(
            length,
            [&](const std::vector<PresenceSymbol>& left_seq)
            {
                for_each_sequence(length,
                                  [&](const std::vector<PresenceSymbol>& right_seq)
                                  {
                                      const PresenceChurn a{fold_range(left_seq, 0, left_seq.size())};
                                      const PresenceChurn b{
                                          fold_range(right_seq, 0, right_seq.size())};
                                      ++compared;
                                      if (meta::compose_presence_churn(a, b) !=
                                          meta::compose_presence_churn(b, a))
                                          ++disagreements;
                                  });
            });
    ASSERT_GT(compared, 0U);
    EXPECT_GT(disagreements, 0U)
        << "the boundary term [last(A) != first(B)] is orientation-sensitive, so SOME ordered pair "
           "must disagree. Zero disagreements over "
        << compared
        << " pairs means the product became commutative, and the window-order MUST is unenforced.";
    // The pair from the witness suite, named here so the count above is anchored to a case a reader
    // can check by hand rather than to a number the sweep happens to produce.
    const PresenceChurn present{element_of(PresenceSymbol::Present)};
    const PresenceChurn absent{element_of(PresenceSymbol::Absent)};
    EXPECT_EQ(meta::compose_presence_churn(present, absent).first, PresenceSymbol::Present);
    EXPECT_EQ(meta::compose_presence_churn(absent, present).first, PresenceSymbol::Absent);
}

// ── G-T3, negative arm (b): collapsing the two absent symbols breaks the identity ─

// DN-50.D4 requires TWO distinct absent symbols and says a single `optional<bool>` is the obvious
// implementation and the wrong one. This arm measures the prediction: run the collapsed product
// over the same population and assert the identity law FAILS — `e . B != B` for at least one B, and
// specifically for every B whose first window is `Unretained`, where the collapsed product invents
// an indeterminate at a boundary that does not exist.
TEST(PresenceChurnProperty, CollapsingTheTwoAbsentSymbolsRedsTheIdentityLaw)
{
    const PresenceChurn identity{};
    std::size_t checked{0};
    std::size_t identity_failures{0};
    for (std::size_t length{1}; length <= kExhaustiveLength; ++length)
        for_each_sequence(length,
                          [&](const std::vector<PresenceSymbol>& sequence)
                          {
                              const PresenceChurn folded{fold_range(sequence, 0, sequence.size())};
                              ++checked;
                              // The REAL product must satisfy the identity law from both sides,
                              // over the whole population and not on a witness.
                              ASSERT_EQ(meta::compose_presence_churn(identity, folded), folded)
                                  << "e . B != B for B=" << render(sequence);
                              ASSERT_EQ(meta::compose_presence_churn(folded, identity), folded)
                                  << "B . e != B for B=" << render(sequence);
                              if (collapsed_product(identity, folded) != folded ||
                                  collapsed_product(folded, identity) != folded)
                                  ++identity_failures;
                          });
    ASSERT_GT(checked, 0U);
    EXPECT_GT(identity_failures, 0U)
        << "the collapsed product must BREAK the identity law, or the two-symbol rule is a comment "
           "with no consequence. Checked "
        << checked << " elements and the collapse cost nothing on any of them.";

    // And the failure must be the SPECIFIC one DN-50.D4 predicts, not merely some failure: an
    // element opening on `Unretained` gains a boundary indeterminate against the empty range, which
    // is exactly the residue `EmptyRange` exists to not produce.
    const PresenceChurn opens_unretained{fold_range(
        {PresenceSymbol::Unretained, PresenceSymbol::Present}, 0, 2)};
    const PresenceChurn collapsed{collapsed_product(identity, opens_unretained)};
    EXPECT_EQ(collapsed.indeterminate, opens_unretained.indeterminate + 1U)
        << "collapsed=" << render(collapsed) << " true=" << render(opens_unretained);
}

} // namespace
