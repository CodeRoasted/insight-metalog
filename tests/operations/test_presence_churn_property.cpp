
// refs: DN-50.D4, DN-50.D8, DN-50.O3
// invariant: this is DN-50.D8's G-T3, the presence-churn monoid proven over a POPULATION; the
// witness suite beside it carries each law on one named witness and cannot say everywhere.
// note: DN-50.D4's associativity is what lets one element per block fold at every ladder level.
// invariant: a broken associativity makes the pyramid read a different number at every level and NO
// determinism gate can see it, because the wrong value is perfectly reproducible.
// invariant: the population is EXHAUSTIVE rather than random, which strengthens DN-50.D8's ask: the
// base alphabet has three inhabitants, so 3^1..3^8 = 9840 sequences are enumerated.
// invariant: every arm compares the fold against an INDEPENDENT reference counted from the symbol
// sequence, never against another bracketing of itself, which would be green on a wrong product.
// note: no RNG in the exhaustive arm; the seeded sweep is mt19937_64 at a literal seed.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using meta::PresenceChurn;
using meta::PresenceSymbol;

// invariant: EmptyRange is excluded by construction -- it is the identity's projection, has span 0,
// and so is not a window and cannot appear in a sequence.
constexpr std::array<PresenceSymbol, 3> kWindowAlphabet{
    PresenceSymbol::Present, PresenceSymbol::Absent, PresenceSymbol::Unretained};

// note: 9840 sequences over lengths 1..8, each folded at every split point, under a second.
constexpr std::size_t kExhaustiveLength{8};
// refs: F-SRC-insight-eidos:detection.api.cppm:PyramidLadderConfig
// note: 64 is more than five times the deepest shipped ladder block, which composes 12 windows.
constexpr std::size_t kSeededMaxLength{64};
constexpr std::size_t kSeededSequences{4000};
// note: the associativity arm is cubic per sequence, so it is bounded to the exhaustive arm's work.
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

// invariant: Unretained at span 1 has no internal boundary, so it carries no indeterminate of its
// own and the residue it produces is always a BOUNDARY term.
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

// refs: DN-50.D4
// invariant: the independent reference, transcribed from the definition and sharing no code with
// compose_presence_churn: churn is the count of boundaries it can READ.
// invariant: a boundary touching Unretained on either side is unreadable and lands in indeterminate
// instead.
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

// invariant: every arm folds through this one function, so a difference between arms is a
// difference in BRACKETING and nothing else.
[[nodiscard]] PresenceChurn fold_range(const std::vector<PresenceSymbol>& sequence,
                                       std::size_t from, std::size_t to)
{
    PresenceChurn out{};
    for (std::size_t i{from}; i < to; ++i)
        out = meta::compose_presence_churn(out, element_of(sequence[i]));
    return out;
}

// refs: DN-50.D4
// invariant: the COLLAPSED product, implemented so the forbidden defect is measured rather than
// described: Unretained and EmptyRange become one symbol.
// invariant: so an empty operand's boundary reads as unreadable, exactly as a truncated one's.
// note: DN-50.D4 predicts this breaks the IDENTITY law; the negative arm holds it to that.
[[nodiscard]] PresenceChurn collapsed_product(const PresenceChurn& earlier,
                                              const PresenceChurn& later)
{
    const auto absent_like{
        [](PresenceSymbol symbol)
        { return symbol == PresenceSymbol::Unretained || symbol == PresenceSymbol::EmptyRange; }};
    const bool boundary_readable{!absent_like(earlier.last) && !absent_like(later.first)};
    return {.span_windows = earlier.span_windows + later.span_windows,
            .transitions = earlier.transitions + later.transitions +
                           (boundary_readable && earlier.last != later.first ? 1U : 0U),
            .indeterminate =
                earlier.indeterminate + later.indeterminate + (boundary_readable ? 0U : 1U),
            .first = earlier.span_windows == 0 ? later.first : earlier.first,
            .last = later.span_windows == 0 ? earlier.last : later.last};
}

// note: enumerate every sequence of this length over the three-symbol alphabet.
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

// invariant: the seeded population is generated once so every arm sweeps the SAME sequences; a red
// in one arm and a green in another is then about the laws, never about two populations.
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

// refs: DN-50.D8
// invariant: the load-bearing property -- for every split point the whole fold equals the product
// of the two half-folds, and both equal the independent reference.
// note: this is what says a ladder level may compose a completed block instead of re-scanning.
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
                              // note: the first counter-example is the diagnosis, the rest noise.
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

// refs: DN-50.D4, DN-50.D8
// invariant: associativity at every ORDERED PAIR of split points -- the general statement the
// witness suite's single triple can only illustrate.
void check_every_associativity(const std::vector<PresenceSymbol>& sequence)
{
    const std::size_t n{sequence.size()};
    for (std::size_t i{0}; i <= n; ++i)
        for (std::size_t j{i}; j <= n; ++j)
        {
            const PresenceChurn a{fold_range(sequence, 0, i)};
            const PresenceChurn b{fold_range(sequence, i, j)};
            const PresenceChurn c{fold_range(sequence, j, n)};
            const PresenceChurn left_first{
                meta::compose_presence_churn(meta::compose_presence_churn(a, b), c)};
            const PresenceChurn right_first{
                meta::compose_presence_churn(a, meta::compose_presence_churn(b, c))};
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
    // invariant: this arm is cubic per sequence, so it sweeps a PREFIX of the shared population and
    // not a second seeded draw; its inputs stay a subset of the arm above's.
    const auto& population{seeded_population()};
    const std::size_t sampled{std::min<std::size_t>(kSeededAssociativitySample, population.size())};
    for (std::size_t n{0}; n < sampled; ++n)
    {
        check_every_associativity(population[n]);
        if (::testing::Test::HasFatalFailure())
            return;
    }
}

// refs: DN-50.O3
// invariant: negative arm (a) -- the product must NOT be commutative, and the MUST has teeth only
// if disagreement is EXHIBITED, so disagreeing pairs are COUNTED over the population.
// note: dropping the orientation from the boundary term passes every positive arm above.
TEST(PresenceChurnProperty, TheProductIsNotCommutativeAndTheSweepExhibitsIt)
{
    std::size_t compared{0};
    std::size_t disagreements{0};
    for (std::size_t length{1}; length <= 4; ++length)
        for_each_sequence(length,
                          [&](const std::vector<PresenceSymbol>& left_seq)
                          {
                              for_each_sequence(length,
                                                [&](const std::vector<PresenceSymbol>& right_seq)
                                                {
                                                    const PresenceChurn a{
                                                        fold_range(left_seq, 0, left_seq.size())};
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
    // note: the witness suite's pair, named so the count above anchors to a hand-checkable case.
    const PresenceChurn present{element_of(PresenceSymbol::Present)};
    const PresenceChurn absent{element_of(PresenceSymbol::Absent)};
    EXPECT_EQ(meta::compose_presence_churn(present, absent).first, PresenceSymbol::Present);
    EXPECT_EQ(meta::compose_presence_churn(absent, present).first, PresenceSymbol::Absent);
}

// refs: DN-50.D4
// invariant: negative arm (b) -- DN-50.D4 requires TWO distinct absent symbols and names the single
// optional<bool> the obvious and wrong implementation; this measures the prediction.
// invariant: the identity law must FAIL under the collapsed product for every element opening on
// Unretained, where it invents an indeterminate at a boundary that does not exist.
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
                              // invariant: the REAL product satisfies identity from both sides over
                              // the whole population.
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

    // invariant: the failure must be the SPECIFIC one predicted and not merely some failure.
    const PresenceChurn opens_unretained{
        fold_range({PresenceSymbol::Unretained, PresenceSymbol::Present}, 0, 2)};
    const PresenceChurn collapsed{collapsed_product(identity, opens_unretained)};
    EXPECT_EQ(collapsed.indeterminate, opens_unretained.indeterminate + 1U)
        << "collapsed=" << render(collapsed) << " true=" << render(opens_unretained);
}

} // namespace
