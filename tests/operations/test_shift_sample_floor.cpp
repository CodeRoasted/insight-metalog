// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// §6.1.1 thin-sample floor CORPUS-PICK + regression guard (studies/003). ordinal_w1's octave
// thresholds are scale-relative (W1 = numerator/(Na·Nb)), so with no absolute-sample gate a
// tiny pairing manufactures latency_shift=HIGH. The resampling scan below draws matched NULL
// pairs (both sides from the SAME representative log2-duration distribution — no real shift) at
// a grid of sample sizes and measures the false-shift rate; the floor is where a null pairing
// stops manufacturing a shift. The scan is OFFLINE (a fixed-seed std::mt19937_64 — this is a
// study over synthetic-but-representative shapes, NOT a determinism surface). The picked value is
// frozen as ComponentOrdinal::kShiftSampleFloor; this file is its permanent regression guard:
// the floor MUST hold the null false-shift rate under target while a real one-octave shift still
// emerges (the positive control — the §7.4 latency_multiplier arm must survive the floor).

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;

// The frozen pick (mirror of ComponentOrdinal::kShiftSampleFloor, which is TU-private in diff.cpp).
constexpr std::uint64_t kFloor{32};
// Pre-registered acceptance targets (fixed BEFORE reading the scan — anti-endogamy).
constexpr double kNullFalseActionableTarget{0.02}; // ≤2% of null pairings may manufacture MED+/HIGH
constexpr double kRealShiftDetectTarget{0.95};     // ≥95% of a real +3-octave regression must emerge
constexpr std::size_t kRealRegressionOctaves{3};   // 8× latency — a clear, actionable regression
constexpr std::size_t kTrials{4000};
constexpr std::size_t kBins{48}; // DurationLog2Ns ladder width

// Representative log2-duration shapes over the 48-bin ladder (bin k ≈ 2^k ns; ~1ms..~1s is bins
// 20..30). Each is a categorical PMF; a real service's per-component latency lives in a handful of
// adjacent octaves with a right tail. Weights need not normalise (discrete_distribution scales).
struct Shape
{
    std::string_view name;
    std::array<double, kBins> pmf;
};

// A log2-latency mode: Gaussian in octave-space centred at `center` with sigma `spread`, plus a
// right (slow) tail. Sigma is in OCTAVES — a real single-component latency spans ~1.5–2 octaves
// p50→p99 (sigma ~1.0–1.5); spreads far beyond that are not one component but an unmixed blend.
[[nodiscard]] std::array<double, kBins> peak_at(double center, double spread, double tail)
{
    std::array<double, kBins> pmf{};
    for (std::size_t bin{0}; bin < kBins; ++bin)
    {
        const double dist{static_cast<double>(bin) - center};
        pmf[bin] = std::exp(-(dist * dist) / (2.0 * spread * spread));
        if (dist > 0.0)
            pmf[bin] += tail / (1.0 + dist * dist); // right (slow) tail
    }
    return pmf;
}

// A genuinely bimodal component (cache hit ~1ms / miss ~64ms) — each mode narrow, not one broad hump.
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

// One draw of N events into a 48-bin histogram from `pmf`.
[[nodiscard]] std::vector<std::uint64_t> sample(const std::array<double, kBins>& pmf, std::size_t n,
                                                std::mt19937_64& rng)
{
    std::discrete_distribution<std::size_t> dist{pmf.begin(), pmf.end()};
    std::vector<std::uint64_t> hist(kBins, 0);
    for (std::size_t draw{0}; draw < n; ++draw)
        ++hist[dist(rng)];
    return hist;
}

// pmf shifted UP the ladder by `octaves` bins — a real, sustained latency regression.
[[nodiscard]] std::array<double, kBins> shift_up(const std::array<double, kBins>& pmf,
                                                 std::size_t octaves)
{
    std::array<double, kBins> out{};
    for (std::size_t bin{0}; bin < kBins; ++bin)
        if (bin + octaves < kBins)
            out[bin + octaves] = pmf[bin];
    return out;
}

// The LOW band (≥0.5 octave) is sampling-noise-dominated for broad distributions and cannot be
// floored away at any tractable N — a real +1-octave shift itself only reads LOW. The floor's job
// is to kill the manufactured ACTIONABLE bands (MED ≥2 octaves = 4×+, HIGH ≥5 octaves) that a tiny
// pairing invents; LOW is accepted as noise-adjacent (the consumer treats it as the weakest band).
[[nodiscard]] bool is_actionable(meta::OrdinalShift shift) noexcept
{
    return shift == meta::OrdinalShift::Med || shift == meta::OrdinalShift::High;
}

// Fraction of NULL pairings (both sides from `pmf`, n each) that manufacture an ACTIONABLE shift.
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

// Fraction of REAL +`octaves`-octave shifts that still emerge as an actionable band.
[[nodiscard]] double real_shift_detect_rate(const std::array<double, kBins>& pmf,
                                            std::size_t octaves, std::size_t n, std::mt19937_64& rng)
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
    {"web-tight", peak_at(23.0, 1.0, 0.05)},        // ~8ms, tight (sigma 1 octave), light tail
    {"service-typical", peak_at(25.0, 1.5, 0.12)},  // ~32ms, sigma 1.5 octaves, moderate slow tail
    {"bimodal-cache", bimodal(20.0, 26.0, 0.30)},   // ~1ms hit / ~64ms miss, 30% miss
}};
} // namespace

// The corpus-pick, run as a scan: print the false-shift rate at each grid point so the freeze is
// reproducible. Always passes — it is the measurement, not the guard.
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

// FROZEN GUARD: at the picked floor, no representative shape manufactures an ACTIONABLE (MED+/HIGH)
// shift from a NULL pairing above target — the 1-vs-1 latency_shift=HIGH pathology is closed.
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

// FROZEN GUARD (positive control): the floor must NOT blind a real regression — a sustained
// +3-octave (8×) latency_multiplier shift still emerges as MED+/HIGH well above chance at the floor.
TEST(ShiftSampleFloorGuard, RealRegressionStillEmergesAtFloor)
{
    std::mt19937_64 rng{0xA11CE999ULL};
    for (const auto& shape : kShapes)
    {
        const double rate{real_shift_detect_rate(shape.pmf, kRealRegressionOctaves, kFloor, rng)};
        EXPECT_GT(rate, kRealShiftDetectTarget)
            << "shape '" << shape.name << "' detects a real +" << kRealRegressionOctaves
            << "-octave regression only " << (rate * 100.0) << "% of the time at the floor N="
            << kFloor << " — the floor is too high (blinds real shifts)";
    }
}

// NOLINTEND
