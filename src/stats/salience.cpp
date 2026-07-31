module;

module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

namespace insight::metalog
{

namespace
{
    // The "looks-like-failure" lexicon — a secondary severity signal for lines
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

    // ── Salience band ladder (0..100) ──────────────────────────────────────────
    // One ladder shared by the severity (level/role/failure-cue), structural-surprise
    // and self-novelty axes, so they are peer signals: Warn 30 … Error 80 … Fatal 100.
    constexpr std::uint32_t kBandFatal{100U};
    constexpr std::uint32_t kBandStrongOffPath{90U}; // surprise: p < 2%
    constexpr std::uint32_t kBandTerminator{90U};    // declared terminator role
    constexpr std::uint32_t kBandError{80U};
    constexpr std::uint32_t kBandOffPath{75U};     // surprise: p < 5%
    constexpr std::uint32_t kBandFailureCue{70U};  // token-lexicon failure word
    constexpr std::uint32_t kBandNoveltyLate{60U}; // first seen in the last 10%
    constexpr std::uint32_t kBandUncommon{50U};    // surprise: p < 10%
    constexpr std::uint32_t kBandNoveltyMid{40U};  // last 25%
    constexpr std::uint32_t kBandWarn{30U};
    constexpr std::uint32_t kBandSomewhatRare{25U}; // surprise: p < 20%
    constexpr std::uint32_t kBandNoveltyEarly{20U}; // last 50%

    // surprise_band inverse-probability thresholds: edge_count·K < outgoing ⇔ p < 1/K.
    constexpr std::uint64_t kInvProb2Pct{50U};
    constexpr std::uint64_t kInvProb5Pct{20U};
    constexpr std::uint64_t kInvProb10Pct{10U};
    constexpr std::uint64_t kInvProb20Pct{5U};

    // novelty_band position thresholds: first_seen·Num > lines·Den ⇔ position > Den/Num.
    constexpr std::uint64_t kNoveltyLast10Num{10U};
    constexpr std::uint64_t kNoveltyLast10Den{9U};

    // rarity modulation values and count·N < lines thresholds (smaller share = rarer).
    constexpr std::uint32_t kRarityRare{100U};    // < 0.1%
    constexpr std::uint32_t kRarityUncommon{90U}; // < 1%
    constexpr std::uint32_t kRarityCommon{60U};   // < 10%
    constexpr std::uint32_t kRarityFrequent{30U}; // >= 10% — likely known/baseline
    constexpr std::uint64_t kRarityTenthPct{1000U};
    constexpr std::uint64_t kRarityOnePct{100U};
    constexpr std::uint64_t kRarityTenPct{10U};
} // namespace

std::optional<LogLevel> dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels)
{
    if (levels.empty())
        return std::nullopt;
    // Severity rank for tie-breaking. Mirrors the LogLevel enum order
    // (Trace<…<Fatal) EXCEPT Unknown — the highest enum value but a
    // "couldn't classify" sentinel, not a severity, so it must LOSE ties to
    // any real level rather than win them.
    constexpr auto severity_rank{
        [](LogLevel level) noexcept -> int
        { return level == LogLevel::Unknown ? -1 : static_cast<int>(level); }};
    auto best_it{levels.begin()};
    for (auto it{std::next(levels.begin())}; it != levels.end(); ++it)
    {
        // Max count; ties broken by higher severity. The tie-break MUST be a
        // pure function of the contents, not unordered_map iteration order —
        // otherwise the dominant level (and thus the per-kind reservoir key)
        // diverges across stdlibs: a tied INFO/ERROR template would resolve to
        // INFO under one stdlib and ERROR under another, hiding a half-error
        // endpoint AND breaking the diagonal. (Pinned by ReservoirDiversity_F10
        // + DeterminismGate.FullDocumentByteIdentityGolden.)
        if (it->second > best_it->second ||
            (it->second == best_it->second &&
             severity_rank(it->first) > severity_rank(best_it->first)))
            best_it = it;
    }
    return best_it->first;
}

std::string dominant_component_of(const std::unordered_map<std::string, std::uint64_t>& components)
{
    const std::string* best{nullptr};
    std::uint64_t best_count{0};
    for (const auto& [component, count] : components)
    {
        // Max count; ties broken by component string asc — a pure function of the
        // contents (not unordered_map iteration order) so the dominant component (and
        // thus the reservoir entry's cube_coord WHERE) is stdlib-identical.
        if (best == nullptr || count > best_count || (count == best_count && component < *best))
        {
            best = &component;
            best_count = count;
        }
    }
    return best == nullptr ? std::string{} : *best;
}

StructuralRole dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles)
{
    StructuralRole best{StructuralRole::None};
    std::uint64_t best_count{0};
    for (const auto& [role, count] : roles)
    {
        // Max count; ties broken by greater StructuralRole enum value, a pure
        // function of the contents (not unordered_map iteration order) so the
        // dominant role is stdlib-identical. Roles rarely tie per template,
        // but determinism must not depend on that. [[transport-determinism-intra-window-order]]
        if (count > best_count || (count == best_count && role > best))
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
    if (edge_count * kInvProb2Pct < source_outgoing)
        return kBandStrongOffPath;
    if (edge_count * kInvProb5Pct < source_outgoing)
        return kBandOffPath;
    if (edge_count * kInvProb10Pct < source_outgoing)
        return kBandUncommon;
    if (edge_count * kInvProb20Pct < source_outgoing)
        return kBandSomewhatRare;
    return 0U; // common transition — on the expected flow
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
    if (first_seen_index * kNoveltyLast10Num > lines * kNoveltyLast10Den)
        return kBandNoveltyLate; // first seen in the last 10% of the window
    if (first_seen_index * 4U > lines * 3U)
        return kBandNoveltyMid; // last 25%
    if (first_seen_index * 2U > lines)
        return kBandNoveltyEarly; // last 50%
    return 0U;                    // present from the first half — not an emergence
}

std::uint32_t salience_score(std::optional<LogLevel> level, StructuralRole role,
                             std::string_view tmpl, bool echoed_source, std::uint64_t count,
                             std::uint64_t lines, std::uint32_t structural_surprise,
                             std::uint32_t novelty) noexcept
{
    // severity 0..100, multi-signal max (robust to any single signal missing).
    std::uint32_t severity{0};
    if (role == StructuralRole::Terminator)
        severity = std::max(severity, kBandTerminator);
    if (level)
    {
        switch (*level)
        {
        case LogLevel::Fatal:
            severity = std::max(severity, kBandFatal);
            break;
        case LogLevel::Error:
            severity = std::max(severity, kBandError);
            break;
        case LogLevel::Warn:
            severity = std::max(severity, kBandWarn);
            break;
        default:
            break;
        }
    }
    // SRC-D-PROV-1 (§3.1): the failure-cue tier is the LEVEL-BLIND re-promoter — it fires on the
    // template text regardless of the per-event level. For an all-echoed template (script source,
    // not a runtime event) A1 already demoted the level to Unknown; skip this tier too, or it
    // re-promotes the echoed `…failed…` template above the real failure. A template seen even once
    // as a runtime event is not all-echoed → the tier (and its genuine level salience) stands.
    if (!echoed_source && looks_like_failure(tmpl))
        severity = std::max(severity, kBandFailureCue);
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
    std::uint32_t rarity{kRarityRare};
    if (lines > 0)
    {
        if (count * kRarityTenthPct < lines)
            rarity = kRarityRare;
        else if (count * kRarityOnePct < lines)
            rarity = kRarityUncommon;
        else if (count * kRarityTenPct < lines)
            rarity = kRarityCommon;
        else
            rarity = kRarityFrequent;
    }
    return severity * rarity; // 0..10000
}

} // namespace insight::metalog
