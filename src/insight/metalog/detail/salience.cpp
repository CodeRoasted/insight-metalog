#include "insight/metalog/detail/salience.hpp"

#include <algorithm>

#include "insight/utils/failure_lexicon.hpp" // F7: token-aware failure lexicon (shared w/ canon)

namespace insight::metalog::detail
{

namespace
{
// The "looks-like-failure" lexicon (F7) — a secondary severity signal for lines
// whose level/role did not already mark them (e.g. a raw `FAILED`/`Traceback`).
// Token-aware (insight::utils::contains_failure_cue): a failure word must be a
// standalone token or a CamelCase `…Error`/`…Exception` type — NOT a substring
// buried in a path/identifier (`Writing tsc-error-report.json`), which used to
// inflate severity and crowd the salience reservoir with benign lines.
[[nodiscard]] bool looks_like_failure(std::string_view tmpl) noexcept
{
    return insight::utils::contains_failure_cue(tmpl);
}

// A structural branch must recur to be trusted: a transition observed ONCE is
// indistinguishable from a one-off / window-boundary artifact (e.g. a novel
// template appended as the last event of a window), and -log2(p) from a single
// sample is unreliable. Requiring ≥2 observations means "this off-path branch
// recurred — it is a real alternate path, not noise."
constexpr std::uint64_t kMinSurpriseEdgeObservations{2};
} // namespace

std::optional<LogLevel> dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels)
{
    if (levels.empty())
        return std::nullopt;
    auto best_it{levels.begin()};
    for (auto it{std::next(levels.begin())}; it != levels.end(); ++it)
    {
        if (it->second > best_it->second)
            best_it = it;
    }
    return best_it->first;
}

StructuralRole dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles)
{
    StructuralRole best{StructuralRole::None};
    std::uint64_t best_count{0};
    for (const auto& [role, count] : roles)
    {
        if (count > best_count)
        {
            best_count = count;
            best = role;
        }
    }
    return best;
}

// Structural-surprise band (0..100) for an edge probability p = c/t, where the
// edge is the MOST-LIKELY incoming transition into a template (so a template is
// surprising only when even its easiest way in is rare). Integer thresholds on
// c·K vs t — no float (I5). c==0 means no incoming edge (a root / unreachable
// node): expected, not surprising. The bands sit alongside the severity ladder
// (Warn 30 … Error 80 … Fatal 100) so a rare off-path transition is a peer
// severity signal, not an afterthought.
std::uint32_t surprise_band(std::uint64_t edge_count, std::uint64_t source_outgoing) noexcept
{
    if (edge_count < kMinSurpriseEdgeObservations || source_outgoing == 0U)
        return 0U;
    if (edge_count * 50U < source_outgoing)
        return 90U; // p < 2%   — strongly off-path
    if (edge_count * 20U < source_outgoing)
        return 75U; // p < 5%
    if (edge_count * 10U < source_outgoing)
        return 50U; // p < 10%
    if (edge_count * 5U < source_outgoing)
        return 25U; // p < 20%
    return 0U;      // common transition — on the expected flow
}

// Self-novelty band (0..100): how late a template first appeared within the
// window, from its first-seen ordinal over the window's line count. A template
// present from the start (first_seen ≈ 0) scores 0; one that EMERGED late scores
// high. Self-relative (I3) and re-derivable from provenance — NOT a baseline diff.
// Same recurrence floor as structural_surprise: a single late event is a
// window-boundary artifact, not an emergence, so require count >= 2. Integer-only
// (cross-multiply, I5). Capped at 60 — softer than severity/structure, since
// late-emergence is suggestive, not intrinsically severe.
std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                           std::uint64_t count) noexcept
{
    if (count < kMinSurpriseEdgeObservations || lines == 0U)
        return 0U;
    if (first_seen_index * 10U > lines * 9U)
        return 60U; // first seen in the last 10% of the window
    if (first_seen_index * 4U > lines * 3U)
        return 40U; // last 25%
    if (first_seen_index * 2U > lines)
        return 20U; // last 50%
    return 0U;      // present from the first half — not an emergence
}

std::uint32_t salience_score(std::optional<LogLevel> level, StructuralRole role,
                             std::string_view tmpl, std::uint64_t count, std::uint64_t lines,
                             std::uint32_t structural_surprise, std::uint32_t novelty) noexcept
{
    // severity 0..100, multi-signal max (robust to any single signal missing).
    std::uint32_t severity{0};
    if (role == StructuralRole::Terminator)
        severity = std::max<std::uint32_t>(severity, 90U);
    if (level)
    {
        switch (*level)
        {
        case LogLevel::Fatal:
            severity = std::max<std::uint32_t>(severity, 100U);
            break;
        case LogLevel::Error:
            severity = std::max<std::uint32_t>(severity, 80U);
            break;
        case LogLevel::Warn:
            severity = std::max<std::uint32_t>(severity, 30U);
            break;
        default:
            break;
        }
    }
    if (looks_like_failure(tmpl))
        severity = std::max<std::uint32_t>(severity, 70U);
    // Severity-confidence tiers run declared > level-keyword > token-lexicon. A
    // DECLARED failure marker (StructuralRole::Terminator, e.g. `##[error]`) would
    // be the highest tier, but it is intentionally NOT gated here: canon already
    // lifts such announced markers to LogLevel::Error, so the level input above
    // captures them. Promote Terminator to its own tier only if a level-escaping
    // marker surfaces in real logs (a non-zero-exit Terminator with no level
    // keyword); until then it is redundant. (Daidalos, 2026-05-31.)
    // structural_surprise and novelty are peer severity axes: a benign Info line is
    // salient if it is reached only via a rare off-path transition (STRUCTURE) or if
    // it just EMERGED late in the window (TIME), even when its level/lexicon
    // severity is 0. Soft max — robust to any single axis being absent.
    severity = std::max({severity, structural_surprise, novelty});
    if (severity == 0U)
        return 0U; // not salient — rarity must never gate a benign template in (SPEC §3.7.2)

    // rarity modulation (a modulator, never a gate): rare → amplify, frequent →
    // damp toward baseline. Integer thresholds on count·N vs lines (no float).
    std::uint32_t rarity{100U};
    if (lines > 0)
    {
        if (count * 1000U < lines)
            rarity = 100U; // < 0.1%  — rare
        else if (count * 100U < lines)
            rarity = 90U; // < 1%
        else if (count * 10U < lines)
            rarity = 60U; // < 10%
        else
            rarity = 30U; // frequent — likely known/baseline
    }
    return severity * rarity; // 0..10000
}

} // namespace insight::metalog::detail
