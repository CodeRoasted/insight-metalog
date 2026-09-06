#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::LogLevel;
using insight::StructuralRole;

// refs: SRC-D-TIR-2
// invariant: label -> template_id_of(label) is injective over these fixtures, so keying by
// TemplateId preserves every asserted value: distinct labels give distinct ids.
[[nodiscard]] std::unordered_map<insight::TemplateId, std::uint64_t>
counts(std::initializer_list<std::pair<std::string_view, std::uint64_t>> items)
{
    std::unordered_map<insight::TemplateId, std::uint64_t> out;
    for (const auto& [label, count] : items)
        out.emplace(insight::template_id_of(label), count);
    return out;
}

TEST(ShannonEntropy, ZeroTotalReturnsZero)
{
    EXPECT_EQ(meta::shannon_entropy_bits({}, 0), 0.0);
    EXPECT_EQ(meta::shannon_entropy_bits({100, 200}, 0), 0.0);
}

TEST(ShannonEntropy, SingleBucketIsZeroBits)
{
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
    const double val = meta::shannon_entropy_bits({999, 1}, 1000);
    EXPECT_GT(val, 0.0);
    EXPECT_LT(val, 0.02);
}

TEST(ShannonEntropy, ZeroBucketsAreSkipped)
{
    const double with_zeros = meta::shannon_entropy_bits({50, 0, 50, 0}, 100);
    const double without_zeros = meta::shannon_entropy_bits({50, 50}, 100);
    EXPECT_NEAR(with_zeros, without_zeros, 1e-9);
}

TEST(Divergences, ZeroTotalReturnsZero)
{
    const auto cur{counts({{"a", 10}})};
    const auto prev{counts({{"a", 10}})};
    const auto result = meta::divergences(cur, 0, prev, 10);
    EXPECT_EQ(result.kl, 0.0);
    EXPECT_EQ(result.js, 0.0);
}

TEST(Divergences, IdenticalDistributionsHaveLowDivergence)
{
    const auto dist{counts({{"a", 50}, {"b", 30}, {"c", 20}})};
    const auto result = meta::divergences(dist, 100, dist, 100);
    EXPECT_NEAR(result.kl, 0.0, 1e-6);
    EXPECT_NEAR(result.js, 0.0, 1e-6);
}

TEST(Divergences, CompletelyDifferentKeysHasHighJs)
{
    const auto cur{counts({{"a", 100}})};
    const auto prev{counts({{"b", 100}})};
    const auto result = meta::divergences(cur, 100, prev, 100);
    EXPECT_GT(result.js, 0.5);
    EXPECT_LE(result.js, 1.0);
}

TEST(Divergences, JsIsInUnitInterval)
{
    const auto cur{counts({{"a", 90}, {"b", 10}})};
    const auto prev{counts({{"b", 80}, {"c", 20}})};
    const auto result = meta::divergences(cur, 100, prev, 100);
    EXPECT_GE(result.js, 0.0);
    EXPECT_LE(result.js, 1.0);
    EXPECT_GE(result.kl, 0.0);
}

TEST(NewAndVanished, IdenticalSetsHaveZeroCounts)
{
    const auto m{counts({{"a", 1}, {"b", 2}})};
    const auto [added, gone] = meta::new_and_vanished(m, m);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(gone, 0u);
}

TEST(NewAndVanished, AllNewKeys)
{
    const auto cur{counts({{"x", 1}, {"y", 1}})};
    const auto prev{counts({{"a", 1}, {"b", 1}})};
    const auto [added, gone] = meta::new_and_vanished(cur, prev);
    EXPECT_EQ(added, 2u);
    EXPECT_EQ(gone, 2u);
}

TEST(NewAndVanished, PartialOverlap)
{
    const auto cur{counts({{"a", 1}, {"b", 1}, {"c", 1}})};
    const auto prev{counts({{"a", 1}, {"d", 1}})};
    const auto [added, gone] = meta::new_and_vanished(cur, prev);
    EXPECT_EQ(added, 2u);
    EXPECT_EQ(gone, 1u);
}

TEST(NewAndVanished, EmptyCurrent)
{
    const auto prev{counts({{"a", 1}, {"b", 1}})};
    const auto [added, gone] = meta::new_and_vanished(counts({}), prev);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(gone, 2u);
}

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
    std::unordered_map<LogLevel, std::uint64_t> levels{{LogLevel::Info, 100}, {LogLevel::Error, 5}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Info);
}

TEST(DominantLevel, TieBreakBySeverityErrorBeatsInfo)
{
    // invariant: at equal counts the tie breaks on severity, never on unordered_map iteration
    // order.
    std::unordered_map<LogLevel, std::uint64_t> levels{{LogLevel::Info, 10}, {LogLevel::Error, 10}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Error);
}

TEST(DominantLevel, TieBreakUnknownLosesToAnyRealLevel)
{
    std::unordered_map<LogLevel, std::uint64_t> levels{{LogLevel::Unknown, 10},
                                                       {LogLevel::Trace, 10}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Trace);
}

TEST(DominantLevel, FatalBeatsAllAtSameCount)
{
    std::unordered_map<LogLevel, std::uint64_t> levels{
        {LogLevel::Info, 5}, {LogLevel::Warn, 5}, {LogLevel::Error, 5}, {LogLevel::Fatal, 5}};
    EXPECT_EQ(meta::dominant_level_of(levels), LogLevel::Fatal);
}

TEST(DominantRole, EmptyReturnsNone)
{
    EXPECT_EQ(meta::dominant_role_of({}), StructuralRole::None);
}

TEST(DominantRole, HighestCountWins)
{
    std::unordered_map<StructuralRole, std::uint64_t> roles{{StructuralRole::GroupBegin, 50},
                                                            {StructuralRole::Terminator, 5}};
    EXPECT_EQ(meta::dominant_role_of(roles), StructuralRole::GroupBegin);
}

TEST(DominantRole, TieBreakByEnumValueIsStable)
{
    // invariant: at equal counts the tie breaks on the higher StructuralRole enum value, never on
    // iteration order.
    std::unordered_map<StructuralRole, std::uint64_t> roles{{StructuralRole::None, 10},
                                                            {StructuralRole::GroupBegin, 10}};
    EXPECT_NE(meta::dominant_role_of(roles), StructuralRole::None);
}

TEST(SurpriseBand, ZeroEdgeCountIsNotSurprising)
{
    EXPECT_EQ(meta::surprise_band(0, 100), 0u);
}

TEST(SurpriseBand, SingleObservationBelowMinFloor)
{
    // note: kMinSurpriseEdgeObservations is 2, so a single observation cannot score.
    EXPECT_EQ(meta::surprise_band(1, 100), 0u);
}

TEST(SurpriseBand, ZeroSourceOutgoingIsNotSurprising)
{
    EXPECT_EQ(meta::surprise_band(5, 0), 0u);
}

TEST(SurpriseBand, CommonTransitionScoresZero)
{
    // refs: F-SRC-insight-metalog:salience.cpp:surprise_band
    EXPECT_EQ(meta::surprise_band(50, 100), 0u);
}

TEST(SurpriseBand, RareBelowTwoPctScoresStrongOffPath)
{
    // note: 2/200 not 1/100: a single observation is below the floor and cannot score.
    EXPECT_EQ(meta::surprise_band(2, 200), 90u);
}

TEST(SurpriseBand, RareBelowFivePctScoresOffPath)
{
    EXPECT_EQ(meta::surprise_band(4, 100), 75u);
}

TEST(SurpriseBand, RareBelowTenPctScoresUncommon)
{
    EXPECT_EQ(meta::surprise_band(9, 100), 50u);
}

TEST(SurpriseBand, RareBelowTwentyPctScoresSomewhatRare)
{
    EXPECT_EQ(meta::surprise_band(15, 100), 25u);
}

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
    // refs: F-SRC-insight-metalog:salience.cpp:novelty_band
    EXPECT_EQ(meta::novelty_band(0, 100, 5), 0u);
}

TEST(NoveltyBand, FirstHalfScoresZero)
{
    EXPECT_EQ(meta::novelty_band(40, 100, 5), 0u);
}

TEST(NoveltyBand, LastFiftyPctScoresEarly)
{
    EXPECT_EQ(meta::novelty_band(60, 100, 5), 20u);
}

TEST(NoveltyBand, LastTwentyFivePctScoresMid)
{
    EXPECT_EQ(meta::novelty_band(80, 100, 5), 40u);
}

TEST(NoveltyBand, LastTenPctScoresLate)
{
    EXPECT_EQ(meta::novelty_band(95, 100, 5), 60u);
}

TEST(SalienceScore, ZeroSeverityReturnsZero)
{
    const auto verdict{meta::salience_score(LogLevel::Info, StructuralRole::None, "conn accepted",
                                            /*echoed_source=*/false, 50, 100, 0, 0)};
    EXPECT_EQ(verdict.score, 0u);
    // post: a zero-score verdict reports NO axis: with no salient axis there is no argmax, and the
    // disengaged optional is itself the answer.
    EXPECT_FALSE(verdict.axis.has_value());
}

TEST(SalienceScore, FatalLevelScoresHigh)
{
    const auto verdict{meta::salience_score(LogLevel::Fatal, StructuralRole::None, "process died",
                                            /*echoed_source=*/false, 1, 1000, 0, 0)};
    EXPECT_GT(verdict.score, 0u);
    // note: 9000 = fatal band 100 x uncommon rarity 90, that tier taken because count*100 < lines.
    EXPECT_EQ(verdict.score, 9000u);
    EXPECT_EQ(verdict.axis, meta::RetentionAxis::Level);
}

TEST(SalienceScore, ErrorLevelScoresLowerThanFatal)
{
    const std::uint32_t fatal_score =
        meta::salience_score(LogLevel::Fatal, StructuralRole::None, "died", /*echoed_source=*/false,
                             1, 1000, 0, 0)
            .score;
    const std::uint32_t error_score =
        meta::salience_score(LogLevel::Error, StructuralRole::None, "failed",
                             /*echoed_source=*/false, 1, 1000, 0, 0)
            .score;
    EXPECT_GT(fatal_score, error_score);
    EXPECT_GT(error_score, 0u);
}

TEST(SalienceScore, FrequentTemplateIsDamped)
{
    // note: rarity is a 4-tier ladder on count/lines; 1/10000 is the top tier, 5000/10000 the last.
    const std::uint32_t rare_score =
        meta::salience_score(LogLevel::Error, StructuralRole::None, "err", /*echoed_source=*/false,
                             1, 10000, 0, 0)
            .score;
    const std::uint32_t frequent_score =
        meta::salience_score(LogLevel::Error, StructuralRole::None, "err", /*echoed_source=*/false,
                             5000, 10000, 0, 0)
            .score;
    EXPECT_GT(rare_score, frequent_score);
}

TEST(SalienceScore, StructuralSurpriseAloneCanMakeNonZero)
{
    const auto verdict{meta::salience_score(LogLevel::Info, StructuralRole::None, "query ok",
                                            /*echoed_source=*/false, 1, 1000, 90, 0)};
    EXPECT_GT(verdict.score, 0u);
    EXPECT_EQ(verdict.axis, meta::RetentionAxis::StructuralSurprise);
}

TEST(SalienceScore, NoveltyAloneCanMakeNonZero)
{
    const auto verdict{meta::salience_score(LogLevel::Info, StructuralRole::None, "session start",
                                            /*echoed_source=*/false, 2, 100, 0, 60)};
    EXPECT_GT(verdict.score, 0u);
    EXPECT_EQ(verdict.axis, meta::RetentionAxis::Novelty);
}

TEST(SalienceScore, EchoedSourceSkipsFailureCueTier)
{
    // refs: SRC-D-PROV-1, DN-64.D3
    // invariant: the failure-cue tier is LEVEL-BLIND, so an echoed-source line already demoted to
    // Unknown must not be re-promoted by it; its runtime peer keeps the cue band.
    // note: the runtime peer's only severity is the token lexicon, so its axis is FailureCue.
    const auto echoed{meta::salience_score(std::nullopt, StructuralRole::None,
                                           "Download failed after 3 attempts",
                                           /*echoed_source=*/true, 1, 1000, 0, 0)};
    EXPECT_EQ(echoed.score, 0u) << "echoed-source failure template must not enter the reservoir";
    const auto runtime{meta::salience_score(std::nullopt, StructuralRole::None,
                                            "Download failed after 3 attempts",
                                            /*echoed_source=*/false, 1, 1000, 0, 0)};
    EXPECT_GT(runtime.score, 0u) << "a real runtime failure template keeps the failure-cue tier";
    EXPECT_EQ(runtime.axis, meta::RetentionAxis::FailureCue);
}

TEST(WireFormat, EpochFormatsCorrectly)
{
    const insight::Timestamp epoch{};
    EXPECT_EQ(meta::format_rfc3339_utc(epoch), "1970-01-01T00:00:00Z");
}

TEST(WireFormat, KnownTimestampFormatsCorrectly)
{
    constexpr std::int64_t kSeconds{1745488800};
    const insight::Timestamp ts{std::chrono::seconds{kSeconds}};
    EXPECT_EQ(meta::format_rfc3339_utc(ts), "2025-04-24T10:00:00Z");
}

TEST(WireFormat, SubSecondTruncatedToSeconds)
{
    const insight::Timestamp ts{std::chrono::milliseconds{500}};
    EXPECT_EQ(meta::format_rfc3339_utc(ts), "1970-01-01T00:00:00Z");
}

TEST(WireFormat, AllLevelsMapToSpecStrings)
{
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Trace), "TRACE");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Debug), "DEBUG");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Info), "INFO");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Warn), "WARN");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Error), "ERROR");
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Fatal), "FATAL");
}

// refs: DN-43.D10, F-SRC-metalog-spec:SPEC.md
// invariant: the two tests below are a PAIR -- the cube's need for a distinct token and the row's
// need to omit are different acts and must never collapse into one.
TEST(WireFormat, UnknownGetsItsOwnCubeAxisTokenRatherThanBorrowingInfo)
{
    // note: SPEC 16.4 reads an ABSENT axis as AGGREGATED, so an unobserved level cannot omit.
    EXPECT_EQ(meta::level_to_spec_string(LogLevel::Unknown), "UNKNOWN");
    EXPECT_NE(meta::level_to_spec_string(LogLevel::Unknown),
              meta::level_to_spec_string(LogLevel::Info));
}

TEST(WireFormat, EveryProducerAbsenceOmitsTheRowLevelMember)
{
    // note: three call spellings collapse to two absences: disengaged, and engaged-but-Unknown.
    EXPECT_FALSE(meta::spec_level_of(std::nullopt).has_value()) << "disengaged optional";
    EXPECT_FALSE(meta::spec_level_of(std::optional{insight::EventLevel{}}).has_value())
        << "engaged optional carrying EventLevel{} — the same fact, the same omission";
    EXPECT_FALSE(
        meta::spec_level_of(std::optional{insight::EventLevel::inferred(LogLevel::Unknown)})
            .has_value());

    EXPECT_EQ(meta::spec_level_of(std::optional{insight::EventLevel::inferred(LogLevel::Error)}),
              std::optional<std::string>{"ERROR"});
    EXPECT_EQ(meta::spec_level_of(std::optional{insight::EventLevel::declared(LogLevel::Error)}),
              std::optional<std::string>{"ERROR"});
}

} // namespace
