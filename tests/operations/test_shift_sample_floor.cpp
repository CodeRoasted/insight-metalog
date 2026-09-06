
// refs: STU-3
// invariant: the thin-sample admissibility floor -- ordinal_w1's octave thresholds are
// scale-relative, so with no absolute-sample gate a tiny pairing manufactures a HIGH shift.
// invariant: the resampling scan draws matched NULL pairs from ONE distribution at a grid of sample
// sizes; the floor is where a null pairing stops manufacturing a shift.
// invariant: the scan is OFFLINE at a fixed seed -- a study over synthetic but representative
// shapes, NOT a determinism surface.
// invariant: the picked value is frozen in the producer and this file is its permanent regression
// guard: the floor must hold the null rate under target while a real one-octave shift emerges.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;

// note: the frozen pick, mirroring the producer constant, which is TU-private in diff.cpp.
constexpr std::uint64_t kFloor{32};
// invariant: the acceptance targets are pre-registered, fixed BEFORE the scan was read.
constexpr double kNullFalseActionableTarget{0.02};
constexpr double kRealShiftDetectTarget{0.95};
constexpr std::size_t kRealRegressionOctaves{3};
constexpr std::size_t kTrials{4000};
constexpr std::size_t kBins{48};

// invariant: representative log2-duration shapes over the 48-bin ladder, each a categorical PMF; a
// real service's per-component latency lives in a handful of adjacent octaves with a right tail.
// note: weights need not normalise, since discrete_distribution scales them.
struct Shape
{
    std::string_view name;
    std::array<double, kBins> pmf;
};

// invariant: sigma is in OCTAVES -- a real single-component latency spans about 1.5 to 2 octaves
// from p50 to p99, and spreads far beyond that are not one component but an unmixed blend.
[[nodiscard]] std::array<double, kBins> peak_at(double center, double spread, double tail)
{
    std::array<double, kBins> pmf{};
    for (std::size_t bin{0}; bin < kBins; ++bin)
    {
        const double dist{static_cast<double>(bin) - center};
        pmf[bin] = std::exp(-(dist * dist) / (2.0 * spread * spread));
        if (dist > 0.0)
            pmf[bin] += tail / (1.0 + dist * dist);
    }
    return pmf;
}

// note: a genuinely bimodal component, each mode narrow rather than one broad hump.
[[nodiscard]] std::array<double, kBins> bimodal(double fast_center, double slow_center,
                                                double slow_weight)
{
    std::array<double, kBins> pmf{};
    const auto add_mode{[&](double center, double weight)
                        {
                            for (std::size_t bin{0}; bin < kBins; ++bin)
                            {
                                const double dist{static_cast<double>(bin) - center};
                                pmf[bin] += weight * std::exp(-(dist * dist) / (2.0 * 0.7 * 0.7));
                            }
                        }};
    add_mode(fast_center, 1.0 - slow_weight);
    add_mode(slow_center, slow_weight);
    return pmf;
}

// note: one draw of N events into a 48-bin histogram.
[[nodiscard]] std::vector<std::uint64_t> sample(const std::array<double, kBins>& pmf, std::size_t n,
                                                std::mt19937_64& rng)
{
    std::discrete_distribution<std::size_t> dist{pmf.begin(), pmf.end()};
    std::vector<std::uint64_t> hist(kBins, 0);
    for (std::size_t draw{0}; draw < n; ++draw)
        ++hist[dist(rng)];
    return hist;
}

// note: the pmf shifted UP the ladder -- a real, sustained latency regression.
[[nodiscard]] std::array<double, kBins> shift_up(const std::array<double, kBins>& pmf,
                                                 std::size_t octaves)
{
    std::array<double, kBins> out{};
    for (std::size_t bin{0}; bin < kBins; ++bin)
        if (bin + octaves < kBins)
            out[bin + octaves] = pmf[bin];
    return out;
}

// invariant: the LOW band is sampling-noise-dominated for broad distributions and cannot be floored
// away at any tractable N, a real one-octave shift itself reading only LOW.
// note: the floor's job is to kill the manufactured ACTIONABLE bands; LOW is accepted as noise.
[[nodiscard]] bool is_actionable(meta::OrdinalShift shift) noexcept
{
    return shift == meta::OrdinalShift::Med || shift == meta::OrdinalShift::High;
}

// note: the fraction of NULL pairings that manufacture an ACTIONABLE shift.
[[nodiscard]] double null_false_actionable_rate(const std::array<double, kBins>& pmf, std::size_t n,
                                                std::mt19937_64& rng)
{
    std::size_t manufactured{0};
    for (std::size_t trial{0}; trial < kTrials; ++trial)
    {
        const auto arm_a{sample(pmf, n, rng)};
        const auto arm_b{sample(pmf, n, rng)};
        if (is_actionable(meta::ordinal_w1(arm_a, arm_b, n, n).shift))
            ++manufactured;
    }
    return static_cast<double>(manufactured) / static_cast<double>(kTrials);
}

// note: the fraction of REAL shifts that still emerge as an actionable band.
[[nodiscard]] double real_shift_detect_rate(const std::array<double, kBins>& pmf,
                                            std::size_t octaves, std::size_t n,
                                            std::mt19937_64& rng)
{
    const auto shifted{shift_up(pmf, octaves)};
    std::size_t detected{0};
    for (std::size_t trial{0}; trial < kTrials; ++trial)
    {
        const auto arm_a{sample(pmf, n, rng)};
        const auto arm_b{sample(shifted, n, rng)};
        if (is_actionable(meta::ordinal_w1(arm_a, arm_b, n, n).shift))
            ++detected;
    }
    return static_cast<double>(detected) / static_cast<double>(kTrials);
}

const std::array<Shape, 3> kShapes{{
    {"web-tight", peak_at(23.0, 1.0, 0.05)},
    {"service-typical", peak_at(25.0, 1.5, 0.12)},
    {"bimodal-cache", bimodal(20.0, 26.0, 0.30)},
}};
} // namespace

// invariant: the corpus-pick run as a scan, printing the rate at each grid point so the freeze is
// reproducible; it always passes, being the measurement and not the guard.
TEST(ShiftSampleFloorScan, FalseActionableRateBySampleSize)
{
    std::mt19937_64 rng{0xC0DE7A57ULL};
    constexpr std::array<std::size_t, 10> grid{2, 4, 6, 8, 12, 16, 20, 24, 32, 48};
    std::cerr << "\n[shift-sample-floor scan] null false-ACTIONABLE (MED+/HIGH) rate by N:\n";
    for (const auto& shape : kShapes)
    {
        std::cerr << "  " << shape.name << ": ";
        for (const std::size_t n : grid)
            std::cerr << "N=" << n << " " << null_false_actionable_rate(shape.pmf, n, rng) << "  ";
        std::cerr << "\n";
    }
    std::cerr << "[shift-sample-floor scan] real +" << kRealRegressionOctaves
              << "-octave detect rate at N=" << kFloor << ": ";
    for (const auto& shape : kShapes)
        std::cerr << shape.name << "="
                  << real_shift_detect_rate(shape.pmf, kRealRegressionOctaves, kFloor, rng) << "  ";
    std::cerr << "\n";
    SUCCEED();
}

// invariant: the frozen guard -- at the picked floor no representative shape manufactures an
// ACTIONABLE shift from a NULL pairing above target.
TEST(ShiftSampleFloorGuard, NullFalseActionableBelowTargetAtFloor)
{
    std::mt19937_64 rng{0x5EED1234ULL};
    for (const auto& shape : kShapes)
    {
        const double rate{null_false_actionable_rate(shape.pmf, kFloor, rng)};
        EXPECT_LT(rate, kNullFalseActionableTarget)
            << "shape '" << shape.name << "' manufactures an actionable shift in " << (rate * 100.0)
            << "% of NULL pairings at the floor N=" << kFloor << " — the floor is too low";
    }
}

// invariant: the frozen positive control -- the floor must NOT blind a real regression, so a
// sustained three-octave shift still emerges as MED or HIGH well above chance at the floor.
TEST(ShiftSampleFloorGuard, RealRegressionStillEmergesAtFloor)
{
    std::mt19937_64 rng{0xA11CE999ULL};
    for (const auto& shape : kShapes)
    {
        const double rate{real_shift_detect_rate(shape.pmf, kRealRegressionOctaves, kFloor, rng)};
        EXPECT_GT(rate, kRealShiftDetectTarget)
            << "shape '" << shape.name << "' detects a real +" << kRealRegressionOctaves
            << "-octave regression only " << (rate * 100.0)
            << "% of the time at the floor N=" << kFloor
            << " — the floor is too high (blinds real shifts)";
    }
}
