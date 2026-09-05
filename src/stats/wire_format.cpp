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
