#pragma once

// Salience scoring: turning a template's level,
// structural role, structural-surprise and self-novelty into a deterministic,
// integer-quantized salience used to admit rare-but-meaningful templates into the
// reservoir. Single responsibility — the severity ⊕ surprise ⊕ novelty ⊗ rarity
// arithmetic (no float in this retention-content path, I5). Shared by the engine
// (close_window reservoir selection) and compose (re-derived reservoir).

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "insight/core/types.hpp" // LogLevel, StructuralRole

namespace insight::metalog::detail
{

// Most-frequent level / structural role for a template (argmax over the count
// map). Roles are deterministic per template, so dominant_role_of is "the" role.
[[nodiscard]] std::optional<LogLevel>
dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels);
[[nodiscard]] StructuralRole
dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles);

// Structural-surprise band (0..100) for the MOST-LIKELY incoming edge p = c/t into
// a template: high only when even a template's easiest way in is rare. Integer
// thresholds on c·K vs t (no float, I5); c==0 means root/unreachable (not surprising).
[[nodiscard]] std::uint32_t surprise_band(std::uint64_t edge_count,
                                          std::uint64_t source_outgoing) noexcept;

// Self-novelty band (0..100): how late a template first appeared within the window
// (first-seen ordinal over line count). Self-relative (I3); integer-only (I5).
[[nodiscard]] std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                                         std::uint64_t count) noexcept;

// Deterministic, quantized salience: (severity ⊕ structural_surprise ⊕ novelty) ⊗
// rarity. Returns 0 for a non-salient template (so rare-benign noise never enters
// the reservoir). Integer math only — no float (I5).
[[nodiscard]] std::uint32_t salience_score(std::optional<LogLevel> level, StructuralRole role,
                                           std::string_view tmpl, std::uint64_t count,
                                           std::uint64_t lines, std::uint32_t structural_surprise,
                                           std::uint32_t novelty) noexcept;

} // namespace insight::metalog::detail
