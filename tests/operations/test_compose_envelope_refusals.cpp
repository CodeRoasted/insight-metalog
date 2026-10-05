// refs: DN-50.D13
// invariant: compose() refuses an envelope it cannot order: a non-empty bound that is not the
// 20-byte RFC 3339 UTC form (state 4), or one bound empty beside a stamped one (state 5).
// invariant: a refusal is std::invalid_argument naming the side, the bound and the value, whatever
// the other input holds and whichever argument position the refused input takes.
// invariant: the two unstamped states stay open -- both unstamped composes to no envelope (state
// 3), and one unstamped carries the other's envelope and duration (state 2).
// refs: DN-137.O1
// invariant: each refusal arm is paired with a no-throw control, so a compose() that refuses
// everything cannot pass this file, and one that refuses nothing cannot either.
// note: a unit test -- compose() is pure over two documents; no RNG, no threads, no wall clock.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using meta::MetaLogDocument;

constexpr std::string_view kStampedStart{"2026-10-01T00:00:00Z"};
constexpr std::string_view kStampedEnd{"2026-10-01T00:01:00Z"};
constexpr std::uint64_t kStampedDuration{60};

// invariant: one malformed spelling per way a foreign producer writes an instant this producer
// does not: a fraction, a numeric offset, and the 19-byte form with no zone at all.
constexpr std::array<std::string_view, 3> kMalformedBounds{
    "2026-10-01T00:00:30.5Z", "2026-10-01T00:00:30+00:00", "2026-10-01T00:00:30"};

enum class Side : std::uint8_t
{
    Lhs,
    Rhs,
};

enum class Bound : std::uint8_t
{
    Start,
    End,
};

[[nodiscard]] std::string_view name_of(Side side)
{
    return side == Side::Lhs ? "lhs" : "rhs";
}

[[nodiscard]] std::string_view name_of(Bound bound)
{
    return bound == Bound::Start ? "start_iso" : "end_iso";
}

[[nodiscard]] MetaLogDocument with_window(std::string_view start_iso, std::string_view end_iso,
                                          std::uint64_t duration_seconds)
{
    MetaLogDocument doc;
    doc.window.envelope = meta::WindowEnvelope{
        .bounds = {.start_iso = std::string{start_iso}, .end_iso = std::string{end_iso}},
        .duration_seconds = duration_seconds};
    return doc;
}

[[nodiscard]] MetaLogDocument stamped()
{
    return with_window(kStampedStart, kStampedEnd, kStampedDuration);
}

[[nodiscard]] MetaLogDocument unstamped()
{
    return MetaLogDocument{};
}

// post: a stamped document whose `bound` is replaced by `value`, the other bound left stamped.
[[nodiscard]] MetaLogDocument stamped_except(Bound bound, std::string_view value)
{
    return bound == Bound::Start ? with_window(value, kStampedEnd, kStampedDuration)
                                 : with_window(kStampedStart, value, kStampedDuration);
}

[[nodiscard]] std::string render_envelope(const MetaLogDocument& doc)
{
    if (!doc.window.envelope)
        return "[no envelope]";
    return "[\"" + doc.window.envelope->bounds.start_iso + "\" .. \"" +
           doc.window.envelope->bounds.end_iso +
           "\"] duration_seconds=" + std::to_string(doc.window.envelope->duration_seconds);
}

// invariant: what one compose() call did -- the refusal's message, or the envelope it composed.
struct ComposeOutcome
{
    std::optional<std::string> refusal;
    std::string composed;
};

[[nodiscard]] ComposeOutcome compose_outcome(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    try
    {
        return ComposeOutcome{.refusal = std::nullopt,
                              .composed = render_envelope(meta::compose(lhs, rhs))};
    }
    catch (const std::invalid_argument& refused)
    {
        return ComposeOutcome{.refusal = std::string{refused.what()}, .composed = {}};
    }
}

// post: the pair in argument order, `refused` placed on `side` and `partner` on the other.
[[nodiscard]] std::pair<MetaLogDocument, MetaLogDocument>
placed(Side side, const MetaLogDocument& refused, const MetaLogDocument& partner)
{
    return side == Side::Lhs ? std::pair{refused, partner} : std::pair{partner, refused};
}

// assert: the refusal names the refused side, the refused bound and its value, and does NOT name
// the other side -- a message naming both would pass a presence-only check.
void expect_refusal_names(const ComposeOutcome& outcome, Side side, Bound bound,
                          std::string_view value, std::string_view label)
{
    ASSERT_TRUE(outcome.refusal.has_value())
        << label << ": compose() accepted the pair and composed " << outcome.composed
        << " -- an envelope it cannot order is published as if it were ordered (DN-50.D13).";
    const std::string& message{*outcome.refusal};
    const Side other{side == Side::Lhs ? Side::Rhs : Side::Lhs};
    const std::string names_bound{std::string{name_of(side)} + " window." +
                                  std::string{name_of(bound)}};
    const std::string names_value{"\"" + std::string{value} + "\""};
    const std::string names_other{std::string{name_of(other)} + " window."};
    EXPECT_NE(message.find(names_bound), std::string::npos)
        << label << ": the refusal must name `" << names_bound << "`. Got: " << message;
    EXPECT_NE(message.find(names_value), std::string::npos)
        << label << ": the refusal must quote the refused value " << names_value
        << ". Got: " << message;
    EXPECT_EQ(message.find(names_other), std::string::npos)
        << label << ": the refusal names the OTHER side `" << names_other
        << "` as well, so it does not say which input to fix. Got: " << message;
}

// refs: DN-50.D13
// invariant: state 4 -- every (side, bound) position, each malformed spelling, and both argument
// orders of the same pair; the partner is fully stamped, so it is never the refused input.
TEST(ComposeEnvelopeRefusal, AMalformedBoundIsRefusedNamingItsSideBoundAndValue)
{
    std::size_t refused_cases{0};
    for (const std::string_view malformed : kMalformedBounds)
        for (const Side side : {Side::Lhs, Side::Rhs})
            for (const Bound bound : {Bound::Start, Bound::End})
            {
                const auto [lhs, rhs]{placed(side, stamped_except(bound, malformed), stamped())};
                const std::string label{"R-ENV1 " + std::string{name_of(side)} + "." +
                                        std::string{name_of(bound)} + "=\"" +
                                        std::string{malformed} + "\""};
                expect_refusal_names(compose_outcome(lhs, rhs), side, bound, malformed, label);
                const Side swapped{side == Side::Lhs ? Side::Rhs : Side::Lhs};
                expect_refusal_names(compose_outcome(rhs, lhs), swapped, bound, malformed,
                                     label + " in compose(B, A)");
                ++refused_cases;
            }
    EXPECT_EQ(refused_cases, kMalformedBounds.size() * 4U)
        << "the arm must visit every (side, bound) position for every malformed spelling.";
}

// refs: DN-50.D13
// invariant: state 5 -- an envelope with one bound empty, on either input and either bound, by a
// stamped then an unstamped partner: eight refusals, never read as unstamped or zero-width.
TEST(ComposeEnvelopeRefusal, AHalfStampedEnvelopeIsRefusedWhateverThePartnerHolds)
{
    const std::array<std::pair<std::string_view, MetaLogDocument>, 2> partners{
        {{"stamped partner", stamped()}, {"unstamped partner", unstamped()}}};
    std::size_t refused_cases{0};
    for (const auto& [partner_name, partner] : partners)
        for (const Side side : {Side::Lhs, Side::Rhs})
            for (const Bound empty_bound : {Bound::Start, Bound::End})
            {
                const auto [lhs, rhs]{placed(side, stamped_except(empty_bound, ""), partner)};
                const std::string label{"R-ENV2 " + std::string{partner_name} + ", " +
                                        std::string{name_of(side)} + "." +
                                        std::string{name_of(empty_bound)} + " empty"};
                expect_refusal_names(compose_outcome(lhs, rhs), side, empty_bound, "", label);
                ++refused_cases;
            }
    EXPECT_EQ(refused_cases, 8U)
        << "state 5 has eight cases: two partners x two sides x two empty bounds.";
}

// refs: DN-50.D13
// invariant: state 3 -- the control that keeps the refusal from swallowing the unstamped case: two
// documents with no envelope compose without a throw, to no envelope.
TEST(ComposeEnvelopeRefusal, TwoUnstampedInputsComposeToNoEnvelopeWithoutAThrow)
{
    const ComposeOutcome outcome{compose_outcome(unstamped(), unstamped())};
    ASSERT_FALSE(outcome.refusal.has_value())
        << "R-ENV2 control: two unstamped documents are state 3, not a refusal. Got: "
        << *outcome.refusal;
    EXPECT_EQ(outcome.composed, render_envelope(unstamped()))
        << "R-ENV2 control: the composed document carries no envelope.";
}

// refs: DN-50.D13
// invariant: state 2 -- one unstamped input carries the other's envelope AND its duration, in both
// argument orders, so the refusal is not a test of "either side has an empty bound".
TEST(ComposeEnvelopeRefusal, OneUnstampedInputCarriesTheOthersEnvelopeAndDuration)
{
    const std::string expected{render_envelope(stamped())};
    for (const Side unstamped_side : {Side::Lhs, Side::Rhs})
    {
        const auto [lhs, rhs]{placed(unstamped_side, unstamped(), stamped())};
        const ComposeOutcome outcome{compose_outcome(lhs, rhs)};
        ASSERT_FALSE(outcome.refusal.has_value())
            << "R-ENV2 control: an unstamped " << name_of(unstamped_side)
            << " beside a stamped partner is state 2, not a refusal. Got: " << *outcome.refusal;
        EXPECT_EQ(outcome.composed, expected)
            << "R-ENV2 control: the unstamped " << name_of(unstamped_side)
            << " must carry the stamped side's envelope and duration unchanged.";
    }
}

} // namespace
