// refs: DN-50.D13
// invariant: parse_rfc3339_utc is the MIRROR of format_rfc3339_utc in SPELLING -- it accepts the
// formatter's 20-byte `YYYY-MM-DDTHH:MM:SSZ` grammar and refuses every other spelling.
// invariant: the oracle for every instant is a LITERAL epoch-seconds count computed outside this
// codebase (calendar.timegm), never a second call into <chrono> or into the function under test.
// invariant: the domain upper bound is the formatter's own: Timestamp is nanoseconds in an int64,
// so its last whole second is 2262-04-11T23:47:16Z and a later instant cannot reach the formatter.
// invariant: the parse's RANGE is wider by design (years to 9999 in sys_seconds), so
// 9999-12-31T23:59:59Z is asserted through the parse alone -- a declared boundary, not a defect.
// note: a unit test of two pure functions; no RNG, no threads, no wall clock.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;

struct WireInstant
{
    std::string_view text;
    std::int64_t epoch_seconds;
};

// invariant: the instants the ruling names, plus the formatter's last representable second.
constexpr std::array<WireInstant, 6> kRoundTripInstants{{
    {.text = "1970-01-01T00:00:00Z", .epoch_seconds = 0},
    {.text = "2000-02-29T12:00:00Z", .epoch_seconds = 951825600},
    {.text = "2024-02-29T23:59:59Z", .epoch_seconds = 1709251199},
    {.text = "2025-12-31T23:59:59Z", .epoch_seconds = 1767225599},
    {.text = "2026-01-01T00:00:00Z", .epoch_seconds = 1767225600},
    {.text = "2262-04-11T23:47:16Z", .epoch_seconds = 9223372036},
}};

constexpr WireInstant kBeyondTimestamp{.text = "9999-12-31T23:59:59Z",
                                       .epoch_seconds = 253402300799};

// invariant: sub-second offsets added to each instant before formatting; the largest is the one
// that still fits an int64 nanosecond count at the last representable second.
constexpr std::array<std::int64_t, 4> kSubSecondNanos{0, 1, 500'000'000, 854'775'807};

[[nodiscard]] std::string render(const std::optional<std::chrono::sys_seconds>& parsed)
{
    return parsed ? std::to_string(parsed->time_since_epoch().count()) + " s since epoch"
                  : std::string{"nullopt"};
}

// refs: DN-50.D13
// invariant: R-RT -- parse(s) equals the literal instant, format(parse(s)) == s, and
// parse(format(t)) equals t floored to the second, over the ruling's instants.
TEST(Rfc3339WireInstant, ParseAndFormatAreMirrorsOverTheFormattersDomain)
{
    for (const WireInstant& instant : kRoundTripInstants)
    {
        const std::chrono::sys_seconds expected{std::chrono::seconds{instant.epoch_seconds}};
        const auto parsed{meta::parse_rfc3339_utc(instant.text)};
        ASSERT_TRUE(parsed.has_value())
            << "R-RT: the formatter's own spelling \"" << instant.text << "\" was refused.";
        EXPECT_EQ(*parsed, expected)
            << "R-RT: \"" << instant.text << "\" parsed to " << render(parsed) << ", expected "
            << instant.epoch_seconds << " s since epoch.";

        const insight::Timestamp at_second{*parsed};
        EXPECT_EQ(meta::format_rfc3339_utc(at_second), instant.text)
            << "R-RT: format(parse(s)) != s for \"" << instant.text << "\".";

        for (const std::int64_t nanos : kSubSecondNanos)
        {
            const insight::Timestamp instant_time{expected + std::chrono::nanoseconds{nanos}};
            const std::string formatted{meta::format_rfc3339_utc(instant_time)};
            const auto reparsed{meta::parse_rfc3339_utc(formatted)};
            EXPECT_EQ(reparsed, std::optional{expected})
                << "R-RT: parse(format(t)) is not t floored to the second for t = \""
                << instant.text << "\" + " << nanos << " ns: the formatter wrote \"" << formatted
                << "\" and the parse read " << render(reparsed) << ".";
        }
    }
}

// refs: DN-50.D13
// invariant: the ruling's domain end, past Timestamp's range: the parse alone is the subject.
TEST(Rfc3339WireInstant, TheRulingsDomainEndParsesToItsLiteralInstant)
{
    const auto parsed{meta::parse_rfc3339_utc(kBeyondTimestamp.text)};
    EXPECT_EQ(parsed, std::optional{std::chrono::sys_seconds{
                          std::chrono::seconds{kBeyondTimestamp.epoch_seconds}}})
        << "R-RT: \"" << kBeyondTimestamp.text << "\" parsed to " << render(parsed) << ", expected "
        << kBeyondTimestamp.epoch_seconds << " s since epoch.";
}

struct Refused
{
    std::string_view why;
    std::string_view text;
};

// invariant: each row is one byte-level departure from the control spelling, so a row passes only
// by the guard its `why` names, and the groups follow the ruling's list in order.
constexpr std::array<Refused, 31> kRefused{{
    {.why = "empty", .text = ""},
    {.why = "19 bytes, no zone", .text = "2026-10-01T00:00:00"},
    {.why = "fractional second", .text = "2026-10-01T00:00:00.5Z"},
    {.why = "fractional millis", .text = "2026-10-01T00:00:00.000Z"},
    {.why = "numeric offset", .text = "2026-10-01T00:00:00+00:00"},
    {.why = "leading space, 21 bytes", .text = " 2026-10-01T00:00:00Z"},
    {.why = "slash date separators", .text = "2026/10/01T00:00:00Z"},
    {.why = "space for T", .text = "2026-10-01 00:00:00Z"},
    {.why = "lowercase t", .text = "2026-10-01t00:00:00Z"},
    {.why = "lowercase z", .text = "2026-10-01T00:00:00z"},
    {.why = "dot clock separators", .text = "2026-10-01T00.00.00Z"},
    {.why = "signed month +1", .text = "2026-+1-01T00:00:00Z"},
    {.why = "signed month -1", .text = "2026--1-01T00:00:00Z"},
    {.why = "signed year +026", .text = "+026-10-01T00:00:00Z"},
    {.why = "signed year -026", .text = "-026-10-01T00:00:00Z"},
    {.why = "signed hour +1", .text = "2026-10-01T+1:00:00Z"},
    {.why = "space-led day", .text = "2026-10- 1T00:00:00Z"},
    {.why = "space-trailed month", .text = "2026-1 -01T00:00:00Z"},
    {.why = "space-led year", .text = " 026-10-01T00:00:00Z"},
    {.why = "space-led second", .text = "2026-10-01T00:00: 0Z"},
    {.why = "February 30", .text = "2026-02-30T00:00:00Z"},
    {.why = "2100 is not a leap year", .text = "2100-02-29T00:00:00Z"},
    {.why = "month 00", .text = "2026-00-01T00:00:00Z"},
    {.why = "month 13", .text = "2026-13-01T00:00:00Z"},
    {.why = "day 00", .text = "2026-10-00T00:00:00Z"},
    {.why = "day 32", .text = "2026-10-32T00:00:00Z"},
    {.why = "hour 24", .text = "2026-10-01T24:00:00Z"},
    {.why = "minute 60", .text = "2026-10-01T00:60:00Z"},
    {.why = "second 60", .text = "2026-10-01T00:00:60Z"},
    {.why = "a real leap second", .text = "2016-12-31T23:59:60Z"},
    {.why = "hour 99", .text = "2026-10-01T99:00:00Z"},
}};

// refs: DN-50.D13
// invariant: R-PARSE -- every spelling the formatter never writes reads nullopt; the leap second
// is refused deliberately, because the parse accepts exactly the formatter's spelling.
TEST(Rfc3339WireInstant, EverySpellingTheFormatterNeverWritesIsRefused)
{
    constexpr std::string_view kControl{"2026-10-01T00:00:00Z"};
    ASSERT_TRUE(meta::parse_rfc3339_utc(kControl).has_value())
        << "R-PARSE control: \"" << kControl << "\" must parse, or every refusal below is the "
        << "parse refusing everything rather than refusing these spellings.";

    std::size_t accepted{0};
    for (const Refused& row : kRefused)
    {
        const auto parsed{meta::parse_rfc3339_utc(row.text)};
        EXPECT_FALSE(parsed.has_value())
            << "R-PARSE (" << row.why << "): \"" << row.text << "\" parsed to " << render(parsed)
            << " -- a spelling the formatter never writes reached an envelope instant.";
        if (parsed)
            ++accepted;
    }
    EXPECT_EQ(accepted, 0U) << "R-PARSE: " << accepted << " of " << kRefused.size()
                            << " foreign spellings were accepted.";
}

} // namespace
