#pragma once

// MetaLog wire-format helpers (spec §2/§3): rendering domain values into the
// exact strings the v0.5.0 envelope requires. Single responsibility — formatting
// only; no statistics, no salience.

#include <string>

#include "insight/core/types.hpp" // Timestamp, LogLevel

namespace insight::metalog::detail
{

// RFC 3339 UTC, fixed widths, always trailing 'Z' (e.g. "2026-04-24T10:00:00Z").
[[nodiscard]] std::string format_rfc3339_utc(Timestamp timestamp);

// SPEC level string. UNKNOWN maps to INFO — the spec defines no UNKNOWN level.
[[nodiscard]] std::string level_to_spec_string(LogLevel level);

} // namespace insight::metalog::detail
