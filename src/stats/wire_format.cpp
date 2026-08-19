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

} // namespace insight::metalog
