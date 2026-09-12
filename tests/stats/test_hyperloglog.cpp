// refs: F-SRC-metalog-spec:SPEC.md
// invariant: the SPEC §3.5.1 producer SHOULD — p = 14, 16 384 registers, standard error at most
// 1.5 % — is witnessed on the sketch itself, never through a document a cap could shape.
// invariant: a standard error is a property of the estimator's SPREAD, which one estimate cannot
// show, so it is measured as a root-mean-square relative error over independent key sets.
// invariant: determinism — every key set is a fixed enumeration and the sketch is integer; the one
// float here is the verdict's arithmetic, never a value the sketch computes.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;

constexpr std::uint8_t kSpecPrecision{14};
constexpr std::size_t kSpecRegisters{16'384};
constexpr double kSpecStandardErrorCeiling{0.015};

// invariant: 32 disjoint key sets per cardinality — the trial label is part of every key, so no
// two trials share a key and each estimate is an independent draw of the hash.
constexpr std::size_t kTrials{32};

// invariant: one point per decade from the linear-counting arm to the raw estimator, including
// 50 000, which sits in the transition band above the linear-counting threshold of 2.5 m.
constexpr std::array<std::uint64_t, 4> kCardinalities{1'000, 10'000, 50'000, 100'000};

// post: the estimate of a sketch fed `cardinality` distinct keys spelled `t<trial>:<index>`.
[[nodiscard]] std::uint64_t estimate_of(std::size_t trial, std::uint64_t cardinality)
{
    meta::HyperLogLog sketch;
    std::array<char, 48> key{};
    for (std::uint64_t index{0}; index < cardinality; ++index)
    {
        char* cursor{key.data()};
        *cursor++ = 't';
        cursor = std::to_chars(cursor, key.data() + key.size(), trial).ptr;
        *cursor++ = ':';
        cursor = std::to_chars(cursor, key.data() + key.size(), index).ptr;
        sketch.add(std::string_view{key.data(), static_cast<std::size_t>(cursor - key.data())});
    }
    return sketch.estimate();
}

} // namespace

TEST(HyperLogLogSketch, PrecisionAndRegisterCountAreTheSpecsDeclaredValues)
{
    EXPECT_EQ(meta::HyperLogLog::kPrecision, kSpecPrecision)
        << "SPEC §3.5.1 names p = 14; the sketch runs p = "
        << static_cast<unsigned>(meta::HyperLogLog::kPrecision);
    EXPECT_EQ(meta::HyperLogLog::kNumRegisters, kSpecRegisters)
        << "SPEC §3.5.1 names 16 384 registers; the sketch keeps "
        << meta::HyperLogLog::kNumRegisters;
}

TEST(HyperLogLogSketch, StandardErrorStaysWithinTheSpecCeilingAcrossFourCardinalities)
{
    const double theoretical{1.04 /
                             std::sqrt(static_cast<double>(meta::HyperLogLog::kNumRegisters))};
    for (const std::uint64_t cardinality : kCardinalities)
    {
        double sum_of_squares{0.0};
        double worst{0.0};
        std::uint64_t worst_estimate{0};
        for (std::size_t trial{0}; trial < kTrials; ++trial)
        {
            const std::uint64_t estimate{estimate_of(trial, cardinality)};
            const double relative{
                (static_cast<double>(estimate) - static_cast<double>(cardinality)) /
                static_cast<double>(cardinality)};
            sum_of_squares += relative * relative;
            if (std::abs(relative) > worst)
            {
                worst = std::abs(relative);
                worst_estimate = estimate;
            }
        }
        const double rms{std::sqrt(sum_of_squares / static_cast<double>(kTrials))};
        EXPECT_LE(rms, kSpecStandardErrorCeiling)
            << "at " << cardinality << " distinct keys over " << kTrials
            << " disjoint key sets the root-mean-square relative error is " << rms * 100.0
            << " %, above SPEC §3.5.1's 1.5 % ceiling (theory at "
            << meta::HyperLogLog::kNumRegisters << " registers: " << theoretical * 100.0
            << " %); worst single estimate " << worst_estimate << " (" << worst * 100.0 << " %)";
    }
}
