module;
// note: gmtime_r is POSIX and absent from import std, so the header stays textual in the GMF.
// refs: ADR-3.D4
#include <ctime>

module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

namespace insight::metalog
{

namespace
{
    // invariant: the byte layout format_rfc3339_utc writes -- `YYYY-MM-DDTHH:MM:SSZ`.
    struct Rfc3339UtcLayout
    {
        static constexpr std::size_t kWidth{20};
        static constexpr std::size_t kYearOffset{0};
        static constexpr std::size_t kYearDigits{4};
        static constexpr std::size_t kMonthOffset{5};
        static constexpr std::size_t kDayOffset{8};
        static constexpr std::size_t kHourOffset{11};
        static constexpr std::size_t kMinuteOffset{14};
        static constexpr std::size_t kSecondOffset{17};
        static constexpr std::size_t kFieldDigits{2};
        static constexpr std::array<std::pair<std::size_t, char>, 6> kSeparators{
            {{4, '-'}, {7, '-'}, {10, 'T'}, {13, ':'}, {16, ':'}, {19, 'Z'}}};
    };

    // post: the unsigned decimal spelled by EXACTLY text[offset, offset + digits), or nullopt when
    // any byte of it is not a digit.
    // pre: offset + digits <= text.size().
    [[nodiscard]] std::optional<unsigned> decimal_field(std::string_view text, std::size_t offset,
                                                        std::size_t digits) noexcept
    {
        const char* const first{text.data() + offset};
        const char* const last{first + digits};
        unsigned value{0};
        const auto [end, error]{std::from_chars(first, last, value)};
        if (error != std::errc{} || end != last)
            return std::nullopt;
        return value;
    }
} // namespace

std::optional<std::chrono::sys_seconds> parse_rfc3339_utc(std::string_view text)
{
    using Layout = Rfc3339UtcLayout;
    if (text.size() != Layout::kWidth)
        return std::nullopt;
    for (const auto& [offset, separator] : Layout::kSeparators)
        if (text[offset] != separator)
            return std::nullopt;
    const auto year{decimal_field(text, Layout::kYearOffset, Layout::kYearDigits)};
    const auto month{decimal_field(text, Layout::kMonthOffset, Layout::kFieldDigits)};
    const auto day{decimal_field(text, Layout::kDayOffset, Layout::kFieldDigits)};
    const auto hour{decimal_field(text, Layout::kHourOffset, Layout::kFieldDigits)};
    const auto minute{decimal_field(text, Layout::kMinuteOffset, Layout::kFieldDigits)};
    const auto second{decimal_field(text, Layout::kSecondOffset, Layout::kFieldDigits)};
    if (!year || !month || !day || !hour || !minute || !second)
        return std::nullopt;
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(*year)},
                                           std::chrono::month{*month}, std::chrono::day{*day}};
    const std::chrono::hours hours{*hour};
    const std::chrono::minutes minutes{*minute};
    const std::chrono::seconds seconds{*second};
    if (!date.ok() || hours >= std::chrono::days{1} || minutes >= std::chrono::hours{1} ||
        seconds >= std::chrono::minutes{1})
        return std::nullopt;
    return std::chrono::sys_days{date} + hours + minutes + seconds;
}

std::string format_rfc3339_utc(Timestamp timestamp)
{
    const auto secs{std::chrono::time_point_cast<std::chrono::seconds>(timestamp)};
    const std::time_t epoch_time{std::chrono::system_clock::to_time_t(secs)};
    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &epoch_time);
#else
    gmtime_r(&epoch_time, &utc_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string level_to_spec_string(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    case LogLevel::Unknown:
    default:
        return "UNKNOWN";
    }
}

std::optional<std::string> spec_level_of(const std::optional<EventLevel>& level)
{
    if (!level || level->value() == LogLevel::Unknown)
        return std::nullopt;
    return level_to_spec_string(level->value());
}

/***************************************************************************************************
D-LSRC-8 — the two wire spellings of RunOutcome are not interchangeable
The same four `insight::RunOutcome` classes reach two wires under two different spellings, and a
consumer takes its spelling from the boundary it actually reads — never from `RunOutcome`, which
has no wire spelling of its own.

  * HERE, the MetaLog document (SPEC §2.5): `success` / `failure` / `unstable` / `aborted`. A
    vendor-neutral standard MINTS that vocabulary, states it is lower-case and CASE-SENSITIVE, and
    `schema/metalog.v0.schema.json` pins it as a CLOSED enum. An upper-case token here is not a
    cosmetic difference: it is a §8 clause-1 schema violation, and `metalog-spec/GOVERNANCE.md` §3
    decides which side moves — the spec wins and the reference implementation is the bug.
  * THERE, the Sift change report (`insight-eidos/sift/src/report/change_report_serialize.cpp`):
    `SUCCESS` / `FAILURE` / `UNSTABLE` / `ABORTED`, rendered by `insight::to_string`, and
    `sift-action/src/types.ts` matches those four literals exactly. That is OUR product format.

REJECTED: align the two by moving Sift onto the spec's spelling. It breaks a published,
customer-facing format to buy a symmetry no consumer asked for. Two namespaces, one internal type,
two serializations — and what the choice costs is paid right here: neither side routes through the
other's renderer.

`insight::to_string` also renders `Unknown` as a token and this wire has none, so the mapping is
partial by construction and `nullopt` means the member is omitted (§2.5).
***************************************************************************************************/
std::optional<std::string> spec_run_outcome_of(RunOutcome outcome)
{
    switch (outcome)
    {
    case RunOutcome::Success:
        return "success";
    case RunOutcome::Failure:
        return "failure";
    case RunOutcome::Unstable:
        return "unstable";
    case RunOutcome::Aborted:
        return "aborted";
    case RunOutcome::Unknown:
    default:
        return std::nullopt;
    }
}

} // namespace insight::metalog
