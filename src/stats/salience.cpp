module;

module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

namespace insight::metalog
{

namespace
{
    [[nodiscard]] bool looks_like_failure(std::string_view tmpl) noexcept
    {
        return insight::utils::contains_failure_cue(tmpl);
    }

    // note: one observation cannot be told from a window-boundary artifact, so an edge must recur.
    constexpr std::uint64_t kMinSurpriseEdgeObservations{2};

    // note: one ladder for all three axes, so severity, structural surprise and novelty are peers.
    constexpr std::uint32_t kBandFatal{100U};
    constexpr std::uint32_t kBandStrongOffPath{90U};
    constexpr std::uint32_t kBandTerminator{90U};
    constexpr std::uint32_t kBandError{80U};
    constexpr std::uint32_t kBandOffPath{75U};
    constexpr std::uint32_t kBandFailureCue{70U};
    constexpr std::uint32_t kBandNoveltyLate{60U};
    constexpr std::uint32_t kBandUncommon{50U};
    constexpr std::uint32_t kBandNoveltyMid{40U};
    constexpr std::uint32_t kBandWarn{30U};
    constexpr std::uint32_t kBandSomewhatRare{25U};
    constexpr std::uint32_t kBandNoveltyEarly{20U};

    // note: an inverse probability -- edge_count * K < source_outgoing means p < 1/K, integer only.
    constexpr std::uint64_t kInvProb2Pct{50U};
    constexpr std::uint64_t kInvProb5Pct{20U};
    constexpr std::uint64_t kInvProb10Pct{10U};
    constexpr std::uint64_t kInvProb20Pct{5U};

    // note: a position threshold -- first_seen * Num > lines * Den puts the ordinal past Den/Num.
    constexpr std::uint64_t kNoveltyLast10Num{10U};
    constexpr std::uint64_t kNoveltyLast10Den{9U};

    // note: rarity modulates and never gates; a smaller share of the window amplifies the score.
    constexpr std::uint32_t kRarityRare{100U};
    constexpr std::uint32_t kRarityUncommon{90U};
    constexpr std::uint32_t kRarityCommon{60U};
    constexpr std::uint32_t kRarityFrequent{30U};
    constexpr std::uint64_t kRarityTenthPct{1000U};
    constexpr std::uint64_t kRarityOnePct{100U};
    constexpr std::uint64_t kRarityTenPct{10U};

    static_assert(kBandFatal * kRarityRare == kSalienceFullScale,
                  "the exported salience full scale must be this ladder's own maximum");
} // namespace

std::optional<LogLevel> dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels)
{
    if (levels.empty())
        return std::nullopt;
    // note: Unknown is the highest enum value but a sentinel, so it ranks below every real level.
    constexpr auto severity_rank{
        [](LogLevel level) noexcept -> int
        { return level == LogLevel::Unknown ? -1 : static_cast<int>(level); }};
    auto best_it{levels.begin()};
    for (auto it{std::next(levels.begin())}; it != levels.end(); ++it)
    {
        // assert: the tie-break is a pure function of the contents, never unordered_map order -- a
        // tied INFO/ERROR template must resolve identically on every stdlib.
        if (it->second > best_it->second ||
            (it->second == best_it->second &&
             severity_rank(it->first) > severity_rank(best_it->first)))
            best_it = it;
    }
    return best_it->first;
}

std::optional<EventLevel>
dominant_event_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels,
                        const std::unordered_map<LogLevel, std::uint64_t>& declared_levels)
{
    const auto dominant{dominant_level_of(levels)};
    if (!dominant)
        return std::nullopt;
    // note: one declared witness is enough; the question is whether anything but a guess backs it.
    const auto witness{declared_levels.find(*dominant)};
    const bool declared{witness != declared_levels.end() && witness->second > 0};
    return declared ? EventLevel::declared(*dominant) : EventLevel::inferred(*dominant);
}

std::string
dominant_component_of(const std::unordered_map<std::string, std::uint64_t, TransparentStringHash,
                                               std::equal_to<>>& components)
{
    const std::string* best{nullptr};
    std::uint64_t best_count{0};
    for (const auto& [component, count] : components)
    {
        // assert: ties break on the component string, never on unordered_map order.
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
        // assert: ties break on the greater enum value, never on unordered_map order.
        if (count > best_count || (count == best_count && role > best))
        {
            best_count = count;
            best = role;
        }
    }
    return best;
}

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
    return 0U;
}

// note: novelty is self-relative to the window, never a baseline diff, and caps below severity.
std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                           std::uint64_t count) noexcept
{
    if (count < kMinSurpriseEdgeObservations || lines == 0U)
        return 0U;
    if (first_seen_index * kNoveltyLast10Num > lines * kNoveltyLast10Den)
        return kBandNoveltyLate;
    if (first_seen_index * 4U > lines * 3U)
        return kBandNoveltyMid;
    if (first_seen_index * 2U > lines)
        return kBandNoveltyEarly;
    return 0U;
}

SalienceVerdict salience_score(std::optional<LogLevel> level, StructuralRole role,
                               std::string_view tmpl, bool echoed_source, std::uint64_t count,
                               std::uint64_t lines, std::uint32_t structural_surprise,
                               std::uint32_t novelty) noexcept
{
    // assert: the call order is the tie-break -- strict > keeps the first axis to offer a band, and
    // kBandTerminator and kBandStrongOffPath both hold 90.
    // refs: DN-64.D3
    SalienceVerdict verdict;
    const auto consider{[&verdict](std::uint32_t band, RetentionAxis axis) noexcept
                        {
                            if (band > verdict.score)
                            {
                                verdict.score = band;
                                verdict.axis = axis;
                            }
                        }};
    // refs: DN-32.D3, SRC-D-PROV-1
    if (role == StructuralRole::Terminator)
        consider(kBandTerminator, RetentionAxis::Terminator);
    if (level)
    {
        switch (*level)
        {
        case LogLevel::Fatal:
            consider(kBandFatal, RetentionAxis::Level);
            break;
        case LogLevel::Error:
            consider(kBandError, RetentionAxis::Level);
            break;
        case LogLevel::Warn:
            consider(kBandWarn, RetentionAxis::Level);
            break;
        default:
            break;
        }
    }
    // assert: an all-echoed template already lost its level to Unknown; skipping this tier stops
    // the echoed text being re-promoted above the real failure.
    // refs: SRC-D-PROV-1
    if (!echoed_source && looks_like_failure(tmpl))
        consider(kBandFailureCue, RetentionAxis::FailureCue);
    // note: structure and time are peer axes -- a benign line reached off-path is salient.
    consider(structural_surprise, RetentionAxis::StructuralSurprise);
    consider(novelty, RetentionAxis::Novelty);
    // note: rarity modulates and never gates, so a template with no salient axis stays out.
    if (verdict.score == 0U)
        return {};

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
    verdict.score *= rarity;
    return verdict;
}

} // namespace insight::metalog
