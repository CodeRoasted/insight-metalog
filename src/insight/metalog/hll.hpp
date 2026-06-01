#pragma once
// HyperLogLog sketch with p=14 (m=16384 registers, ~1.5% standard error).
// Zero-dependency implementation; FNV-1a + MurmurHash3 finalizer for hash.
// Private to the metalog module — not part of the public API.

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

#include "insight/math/det_math.hpp" // F5-M5: deterministic ln, no libm

namespace insight::metalog::detail
{

class HyperLogLog
{
  public:
    static constexpr std::uint8_t kPrecision = 14;
    static constexpr std::size_t kNumRegisters{1U << kPrecision}; // 16384

    HyperLogLog() noexcept
    {
        regs_.fill(0);
    }

    void add(std::string_view value) noexcept
    {
        const auto hash_val = hash64(value);
        const auto idx = static_cast<std::size_t>(hash_val >> (64U - kPrecision));
        // Remaining bits: shift left by kPrecision so we count leading zeros
        // in the bottom (64-kPrecision) bits — map them to positions 1..50.
        const auto hash_rem = hash_val << kPrecision;
        // +1 because rho(hash_rem) counts from 1.
        const auto rho = static_cast<std::uint8_t>(
            (hash_rem == 0U) ? (64U - kPrecision + 1U) : (std::countl_zero(hash_rem) + 1U));
        // NOLINTNEXTLINE(readability-use-std-min-max) — hot path
        if (rho > regs_[idx])
            regs_[idx] = rho;
    }

    [[nodiscard]] std::uint64_t estimate() const noexcept
    {
        // F5-M5: the harmonic sum Σ 2^(−rⱼ) is an EXACT dyadic fixed-point sum.
        // Registers are integers in [0, 64−kPrecision+1] = [0, 51], so each term
        // 2^(−reg) is an exact power of two; accumulating 2^(kHllFrac−reg) in
        // __int128 has no rounding and no order dependence (unlike the prior
        // double Σ std::ldexp, which dropped low-order terms). No libm.
        constexpr int kHllFrac{52}; // > max register value (51) → every term exact
        unsigned __int128 sum_fixed{0};
        int zeros{0};
        for (auto reg : regs_)
        {
            sum_fixed += static_cast<unsigned __int128>(1) << (kHllFrac - static_cast<int>(reg));
            if (reg == 0)
                ++zeros;
        }

        // raw = α·m²/S = (α·m²·2^kHllFrac) / S_fixed. The numerator is α's mantissa
        // scaled by exact powers of two → an EXACT, compiler-identical double;
        // the conversion to __int128 is exact, and one integer divide yields raw.
        constexpr double kAlpha{0.7213 / (1.0 + (1.079 / static_cast<double>(kNumRegisters)))};
        const double raw_numerator{kAlpha * static_cast<double>(kNumRegisters) *
                                   static_cast<double>(kNumRegisters) *
                                   static_cast<double>(std::uint64_t{1} << kHllFrac)};
        // sum_fixed > 0 always: regs_ is a fixed, compile-time-non-empty std::array
        // (kNumRegisters > 0) and every term 1<<(kHllFrac-reg) is >= 2, so the loop
        // above accumulates a strictly positive sum. The analyzer cannot prove the
        // range-for executes; this divide is never by zero.
        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        const unsigned __int128 raw{static_cast<unsigned __int128>(raw_numerator) / sum_fixed};

        // Small-range correction (linear counting): m·ln(m/zeros), via det_ln.
        constexpr std::uint64_t kSmallRangeThreshold{(5U * kNumRegisters) / 2U}; // 2.5·m
        if (zeros > 0 && raw < kSmallRangeThreshold)
        {
            // ln(m/zeros) = ln(m) − ln(zeros), each in Qk; m·(…) then >> kFracBits.
            const __int128 linear{static_cast<__int128>(kNumRegisters) *
                                  (insight::det::det_ln_fixed(kNumRegisters) -
                                   insight::det::det_ln_fixed(static_cast<std::uint64_t>(zeros)))};
            return static_cast<std::uint64_t>(linear >> insight::det::kFracBits);
        }
        return static_cast<std::uint64_t>(raw);
    }

    void reset() noexcept
    {
        regs_.fill(0);
    }

  private:
    std::array<std::uint8_t, kNumRegisters> regs_{};

    [[nodiscard]] static std::uint64_t hash64(std::string_view str) noexcept
    {
        // FNV-1a 64-bit.
        constexpr std::uint64_t kFnvOffsetBasis{0xcbf29ce484222325ULL};
        constexpr std::uint64_t kFnvPrime{0x00000100000001b3ULL};
        constexpr std::uint64_t kMurmurFinalMix1{0xff51afd7ed558ccdULL};
        constexpr std::uint64_t kMurmurFinalMix2{0xc4ceb9fe1a85ec53ULL};
        constexpr std::uint64_t kMixShift{33U};
        std::uint64_t hash_acc = kFnvOffsetBasis;
        for (const unsigned char byte : str)
        {
            hash_acc ^= static_cast<std::uint64_t>(byte);
            hash_acc *= kFnvPrime;
        }
        // MurmurHash3 64-bit finalizer for better avalanche.
        hash_acc ^= hash_acc >> kMixShift;
        hash_acc *= kMurmurFinalMix1;
        hash_acc ^= hash_acc >> kMixShift;
        hash_acc *= kMurmurFinalMix2;
        hash_acc ^= hash_acc >> kMixShift;
        return hash_acc;
    }
};

} // namespace insight::metalog::detail
