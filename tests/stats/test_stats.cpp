// NOLINTBEGIN
// Unit tests for insight.metalog.detail.stats:
//   statistics.cpp — shannon_entropy_bits, divergences, new_and_vanished, histogram_js
//   salience.cpp   — dominant_level_of, dominant_role_of, surprise_band, novelty_band, salience_score
//   wire_format.cpp — format_rfc3339_utc, level_to_spec_string

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::LogLevel;
using insight::StructuralRole;

// ── shannon_entropy_bits ──────────────────────────────────────────────────────

TEST(ShannonEntropy, ZeroTotalReturnsZero)
{
    EXPECT_EQ(meta::shannon_entropy_bits({}, 0), 0.0);
    EXPECT_EQ(meta::shannon_entropy_bits({100, 200}, 0), 0.0);
}

TEST(ShannonEntropy, SingleBucketIsZeroBits)
{
    // One bucket with all the mass — no uncertainty.
    EXPECT_NEAR(meta::shannon_entropy_bits({42}, 42), 0.0, 1e-9);
}

TEST(ShannonEntropy, TwoEqualBucketsIsOneBit)
{
    EXPECT_NEAR(meta::shannon_entropy_bits({50, 50}, 100), 1.0, 1e-6);
}

TEST(ShannonEntropy, FourEqualBucketsIsTwoBits)
{
    EXPECT_NEAR(meta::shannon_entropy_bits({25, 25, 25, 25}, 100), 2.0, 1e-6);
}

TEST(ShannonEntropy, HighlySkewedIsLowEntropy)
{
    // 999 in one bucket, 1 in another — very low entropy.
    const double val = meta::shannon_entropy_bits({999, 1}, 1000);
    EXPECT_GT(val, 0.0);
    EXPECT_LT(val, 0.02);
}

TEST(ShannonEntropy, ZeroBucketsAreSkipped)
{
    // Sparse: zeros should not affect result vs. the same non-zero buckets.
    const double with_zeros    = meta::shannon_entropy_bits({50, 0, 50, 0}, 100);
    const double without_zeros = meta::shannon_entropy_bits({50, 50}, 100);
    EXPECT_NEAR(with_zeros, without_zeros, 1e-9);
}

// ── divergences ──────────────────────────────────────────────────────────────

TEST(Divergences, ZeroTotalReturnsZero)
{
    std::unordered_map<std::string, std::uint64_t> cur{{"a", 10}};
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 10}};
    const auto result = meta::divergences(cur, 0, prev, 10);
    EXPECT_EQ(result.kl, 0.0);
    EXPECT_EQ(result.js, 0.0);
}

TEST(Divergences, IdenticalDistributionsHaveLowDivergence)
{
    std::unordered_map<std::string, std::uint64_t> dist{{"a", 50}, {"b", 30}, {"c", 20}};
    const auto result = meta::divergences(dist, 100, dist, 100);
    EXPECT_NEAR(result.kl, 0.0, 1e-6);
    EXPECT_NEAR(result.js, 0.0, 1e-6);
}

TEST(Divergences, CompletelyDifferentKeysHasHighJs)
{
    std::unordered_map<std::string, std::uint64_t> cur{{"a", 100}};
    std::unordered_map<std::string, std::uint64_t> prev{{"b", 100}};
    const auto result = meta::divergences(cur, 100, prev, 100);
    EXPECT_GT(result.js, 0.5);
    EXPECT_LE(result.js, 1.0);
}

TEST(Divergences, JsIsInUnitInterval)
{
    std::unordered_map<std::string, std::uint64_t> cur{{"a", 90}, {"b", 10}};
    std::unordered_map<std::string, std::uint64_t> prev{{"b", 80}, {"c", 20}};
    const auto result = meta::divergences(cur, 100, prev, 100);
    EXPECT_GE(result.js, 0.0);
    EXPECT_LE(result.js, 1.0);
    EXPECT_GE(result.kl, 0.0);
}

// ── new_and_vanished ──────────────────────────────────────────────────────────

TEST(NewAndVanished, IdenticalSetsHaveZeroCounts)
{
    std::unordered_map<std::string, std::uint64_t> m{{"a", 1}, {"b", 2}};
    const auto [added, gone] = meta::new_and_vanished(m, m);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(gone, 0u);
}

TEST(NewAndVanished, AllNewKeys)
{
    std::unordered_map<std::string, std::uint64_t> cur{{"x", 1}, {"y", 1}};
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 1}, {"b", 1}};
    const auto [added, gone] = meta::new_and_vanished(cur, prev);
    EXPECT_EQ(added, 2u);
    EXPECT_EQ(gone, 2u);
}

TEST(NewAndVanished, PartialOverlap)
{
    std::unordered_map<std::string, std::uint64_t> cur{{"a", 1}, {"b", 1}, {"c", 1}};
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 1}, {"d", 1}};
    const auto [added, gone] = meta::new_and_vanished(cur, prev);
    EXPECT_EQ(added, 2u); // b, c are new
    EXPECT_EQ(gone, 1u);  // d vanished
}

TEST(NewAndVanished, EmptyCurrent)
{
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 1}, {"b", 1}};
    const auto [added, gone] = meta::new_and_vanished({}, prev);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(gone, 2u);
}

// ── histogram_js ─────────────────────────────────────────────────────────────

TEST(HistogramJs, ZeroTotalReturnsZero)
{
    std::unordered_map<std::string, std::uint64_t> m{{"a", 10}};
    EXPECT_EQ(meta::histogram_js(m, 0, m, 10), 0.0);
    EXPECT_EQ(meta::histogram_js(m, 10, m, 0), 0.0);
}

TEST(HistogramJs, IdenticalIsNearZero)
{
    std::unordered_map<std::string, std::uint64_t> m{{"a", 50}, {"b", 50}};
    EXPECT_NEAR(meta::histogram_js(m, 100, m, 100), 0.0, 1e-6);
}

TEST(HistogramJs, DisjointKeysIsHigh)
{
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 100}};
    std::unordered_map<std::string, std::uint64_t> curr{{"b", 100}};
    const double val = meta::histogram_js(prev, 100, curr, 100);
    EXPECT_GT(val, 0.5);
    EXPECT_LE(val, 1.0);
}

TEST(HistogramJs, ResultIsInUnitInterval)
{
    std::unordered_map<std::string, std::uint64_t> prev{{"a", 70}, {"b", 30}};
    std::unordered_map<std::string, std::uint64_t> curr{{"b", 60}, {"c", 40}};
    const double val = meta::histogram_js(prev, 100, curr, 100);
    EXPECT_GE(val, 0.0);
    EXPECT_LE(val, 1.0);
}

// ── dominant_level_of ─────────────────────────────────────────────────────────

TEST(DominantLevel, EmptyReturnsNullopt)
{
    EXPECT_EQ(meta::dominant_level_of({}), std::nullopt);
}

TEST(DominantLevel, SingleLevelReturnsThatLevel)
{
    EXPECT_EQ(meta::dominant_level_of({{LogLevel::Error, 5}}), LogLevel::Error);
}

TEST(DominantLevel, HighestCountWins)
{
    std::unordered_map<LogLevel, std::uint64_t> levels{
        {LogLevel::Info, 100}, {LogLevel::Error, 5}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Info);
}

TEST(DominantLevel, TieBreakBySeverityErrorBeatsInfo)
{
    // Tied count — higher severity must win, not map iteration order.
    std::unordered_map<LogLevel, std::uint64_t> levels{
        {LogLevel::Info, 10}, {LogLevel::Error, 10}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Error);
}

TEST(DominantLevel, TieBreakUnknownLosesToAnyRealLevel)
{
    std::unordered_map<LogLevel, std::uint64_t> levels{
        {LogLevel::Unknown, 10}, {LogLevel::Trace, 10}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Trace);
}

TEST(DominantLevel, FatalBeatsAllAtSameCount)
{
    std::unordered_map<LogLevel, std::uint64_t> levels{
        {LogLevel::Info, 5}, {LogLevel::Warn, 5}, {LogLevel::Error, 5}, {LogLevel::Fatal, 5}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Fatal);
}

// ── dominant_role_of ─────────────────────────────────────────────────────────

TEST(DominantRole, EmptyReturnsNone)
{
    EXPECT_EQ(meta::dominant_role_of({}), StructuralRole::None);
}

TEST(DominantRole, HighestCountWins)
{
    std::unordered_map<StructuralRole, std::uint64_t> roles{
        {StructuralRole::GroupBegin, 50}, {StructuralRole::Terminator, 5}};
    EXPECT_EQ(meta::dominant_role_of(roles), StructuralRole::GroupBegin);
}

TEST(DominantRole, TieBreakByEnumValueIsStable)
{
    // Tie: higher enum value must win regardless of iteration order.
    std::unordered_map<StructuralRole, std::uint64_t> roles{
        {StructuralRole::None, 10}, {StructuralRole::GroupBegin, 10}};
    // Milestone > None in enum value — Milestone must win.
    EXPECT_NE(meta::dominant_role_of(roles), StructuralRole::None);
}

// ── surprise_band ─────────────────────────────────────────────────────────────

TEST(SurpriseBand, ZeroEdgeCountIsNotSurprising)
{
    EXPECT_EQ(meta::surprise_band(0, 100), 0u);
}

TEST(SurpriseBand, SingleObservationBelowMinFloor)
{
    // kMinSurpriseEdgeObservations = 2; one observation must not score.
    EXPECT_EQ(meta::surprise_band(1, 100), 0u);
}

TEST(SurpriseBand, ZeroSourceOutgoingIsNotSurprising)
{
    EXPECT_EQ(meta::surprise_band(5, 0), 0u);
}

TEST(SurpriseBand, CommonTransitionScoresZero)
{
    // p = 50/100 = 50% — well above every threshold.
    EXPECT_EQ(meta::surprise_band(50, 100), 0u);
}

TEST(SurpriseBand, RareBelowTwoPctScoresStrongOffPath)
{
    // p = 1/100 = 1% → kBandStrongOffPath = 90.
    EXPECT_EQ(meta::surprise_band(2, 200), 90u);
}

TEST(SurpriseBand, RareBelowFivePctScoresOffPath)
{
    // p = 4/100 = 4% → kBandOffPath = 75.
    EXPECT_EQ(meta::surprise_band(4, 100), 75u);
}

TEST(SurpriseBand, RareBelowTenPctScoresUncommon)
{
    // p = 9/100 = 9% → kBandUncommon = 50.
    EXPECT_EQ(meta::surprise_band(9, 100), 50u);
}

TEST(SurpriseBand, RareBelowTwentyPctScoresSomewhatRare)
{
    // p = 15/100 = 15% → kBandSomewhatRare = 25.
    EXPECT_EQ(meta::surprise_band(15, 100), 25u);
}

// ── novelty_band ─────────────────────────────────────────────────────────────

TEST(NoveltyBand, ZeroLinesIsNotNovel)
{
    EXPECT_EQ(meta::novelty_band(50, 0, 5), 0u);
}

TEST(NoveltyBand, SingleObservationBelowMinFloor)
{
    EXPECT_EQ(meta::novelty_band(90, 100, 1), 0u);
}

TEST(NoveltyBand, PresentFromStartScoresZero)
{
    // first_seen = 0, lines = 100 → position 0% — not an emergence.
    EXPECT_EQ(meta::novelty_band(0, 100, 5), 0u);
}

TEST(NoveltyBand, FirstHalfScoresZero)
{
    // first_seen = 40, lines = 100 → position 40% — not in the late window.
    EXPECT_EQ(meta::novelty_band(40, 100, 5), 0u);
}

TEST(NoveltyBand, LastFiftyPctScoresEarly)
{
    // first_seen = 60, lines = 100 → position 60% → kBandNoveltyEarly = 20.
    EXPECT_EQ(meta::novelty_band(60, 100, 5), 20u);
}

TEST(NoveltyBand, LastTwentyFivePctScoresMid)
{
    // first_seen = 80, lines = 100 → position 80% → kBandNoveltyMid = 40.
    EXPECT_EQ(meta::novelty_band(80, 100, 5), 40u);
}

TEST(NoveltyBand, LastTenPctScoresLate)
{
    // first_seen = 95, lines = 100 → position 95% → kBandNoveltyLate = 60.
    EXPECT_EQ(meta::novelty_band(95, 100, 5), 60u);
}

// ── salience_score ────────────────────────────────────────────────────────────

TEST(SalienceScore, ZeroSeverityReturnsZero)
{
    // Info level, no failure cue, no surprise, no novelty → 0.
    EXPECT_EQ(meta::salience_score(LogLevel::Info, StructuralRole::None, "conn accepted",
                                   50, 100, 0, 0), 0u);
}

TEST(SalienceScore, FatalLevelScoresHigh)
{
    const std::uint32_t score = meta::salience_score(
        LogLevel::Fatal, StructuralRole::None, "process died", 1, 1000, 0, 0);
    EXPECT_GT(score, 0u);
    // fatal band (100) × uncommon (90, count·100 < lines) = 9000.
    EXPECT_EQ(score, 9000u);
}

TEST(SalienceScore, ErrorLevelScoresLowerThanFatal)
{
    const std::uint32_t fatal_score = meta::salience_score(
        LogLevel::Fatal, StructuralRole::None, "died", 1, 1000, 0, 0);
    const std::uint32_t error_score = meta::salience_score(
        LogLevel::Error, StructuralRole::None, "failed", 1, 1000, 0, 0);
    EXPECT_GT(fatal_score, error_score);
    EXPECT_GT(error_score, 0u);
}

TEST(SalienceScore, FrequentTemplateIsDamped)
{
    // Same level but frequent (count >= 10% of lines) → lower rarity modulator.
    const std::uint32_t rare_score = meta::salience_score(
        LogLevel::Error, StructuralRole::None, "err", 1, 10000, 0, 0);
    const std::uint32_t frequent_score = meta::salience_score(
        LogLevel::Error, StructuralRole::None, "err", 5000, 10000, 0, 0);
    EXPECT_GT(rare_score, frequent_score);
}

TEST(SalienceScore, StructuralSurpriseAloneCanMakeNonZero)
{
    // Info template, no failure cue — but strong surprise lifts it.
    const std::uint32_t score = meta::salience_score(
        LogLevel::Info, StructuralRole::None, "query ok", 1, 1000, 90, 0);
    EXPECT_GT(score, 0u);
}

TEST(SalienceScore, NoveltyAloneCanMakeNonZero)
{
    const std::uint32_t score = meta::salience_score(
        LogLevel::Info, StructuralRole::None, "session start", 2, 100, 0, 60);
    EXPECT_GT(score, 0u);
}

// ── format_rfc3339_utc ────────────────────────────────────────────────────────

TEST(WireFormat, EpochFormatsCorrectly)
{
    const insight::Timestamp epoch{};
    EXPECT_EQ(meta::format_rfc3339_utc(epoch), "1970-01-01T00:00:00Z");
}

TEST(WireFormat, KnownTimestampFormatsCorrectly)
{
    // 2025-04-24T10:00:00Z = 1745488800 seconds since epoch.
    constexpr std::int64_t kSeconds{1745488800};
    const insight::Timestamp ts{std::chrono::seconds{kSeconds}};
    EXPECT_EQ(meta::format_rfc3339_utc(ts), "2025-04-24T10:00:00Z");
}

TEST(WireFormat, SubSecondTruncatedToSeconds)
{
    // 500ms past epoch should still render as 00:00:00Z.
    const insight::Timestamp ts{std::chrono::milliseconds{500}};
    EXPECT_EQ(meta::format_rfc3339_utc(ts), "1970-01-01T00:00:00Z");
}

// ── level_to_spec_string ──────────────────────────────────────────────────────

TEST(WireFormat, AllLevelsMapToSpecStrings)
{
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Trace),   "TRACE");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Debug),   "DEBUG");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Info),    "INFO");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Warn),    "WARN");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Error),   "ERROR");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Fatal),   "FATAL");
}

TEST(WireFormat, UnknownMapsToInfo)
{
    // SPEC §3: no UNKNOWN level — falls back to INFO.
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Unknown), "INFO");
}

} // namespace

// NOLINTEND
