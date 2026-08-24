module;
#include <ctime> // gmtime_r — POSIX, not in import std (ADR-3.D4 textual GMF exception)

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
        // The spec defining no UNKNOWN level is true and licenses nothing: a wire ROW does not need
        // an UNKNOWN token, it needs the member OMITTED (spec_level_of). A cube COORD does need a
        // token, because omitting an axis there already means "aggregated over all levels" — a
        // different fact. §16.4 requires only that a value be a string, so a distinct one is legal.
        return "UNKNOWN";
    }
}

std::optional<std::string> spec_level_of(const std::optional<EventLevel>& level)
{
    if (!level || level->value() == LogLevel::Unknown)
        return std::nullopt;
    return level_to_spec_string(level->value());
}

// ── The two spellings of one enum, and why they are not interchangeable ───────────────────────
// The same four `insight::RunOutcome` classes reach two different wires under two different
// spellings. That is deliberate, and the hazard it creates is a reader assuming a token from one
// can be pasted into the other.
//
//   * HERE — the MetaLog document, SPEC §2.5 — `success`/`failure`/`unstable`/`aborted`. The
//     vocabulary is MINTED by a vendor-neutral standard which states it is lower-case and
//     CASE-SENSITIVE, and `schema/metalog.v0.schema.json` pins it as a CLOSED enum. An upper-case
//     token here is not a cosmetic difference: it is a §8 clause-1 schema violation, and
//     `metalog-spec/GOVERNANCE.md` §3 decides which side moves — the spec wins and the reference
//     implementation is the bug.
//   * THERE — the Sift change report, `insight-eidos/sift/src/report/change_report_serialize.cpp`
//     — `SUCCESS`/`FAILURE`/`UNSTABLE`/`ABORTED`, rendered by `insight::to_string`. That is OUR
//     product format, and `sift-action/src/types.ts` matches those four literals exactly.
//
// The rejected alternative was to align them by moving Sift onto the spec's spelling: that breaks
// a published customer-facing format to buy a symmetry no consumer asked for. Two namespaces, one
// internal type, two serializations. What the choice costs is paid here: neither side routes
// through the other's renderer, and a consumer takes its spelling from the boundary it actually
// reads — never from `RunOutcome`, which has no wire spelling of its own.
//
// `insight::to_string` also renders `Unknown` as a token; this wire has none, so the mapping is
// partial by construction and `nullopt` means the member is omitted (§2.5).
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
