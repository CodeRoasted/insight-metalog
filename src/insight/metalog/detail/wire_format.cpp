#include "insight/metalog/detail/wire_format.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace insight::metalog::detail
{

std::string format_rfc3339_utc(Timestamp timestamp)
{
    const auto secs{std::chrono::time_point_cast<std::chrono::seconds>(timestamp)};
    const std::time_t epoch_time{std::chrono::system_clock::to_time_t(secs)};
    std::tm utc_tm{};
#if defined(_WIN32)
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
        return "INFO"; // spec doesn't define UNKNOWN
    }
}

} // namespace insight::metalog::detail
