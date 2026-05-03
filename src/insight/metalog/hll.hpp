#pragma once
// HyperLogLog sketch with p=14 (m=16384 registers, ~1.5% standard error).
// Zero-dependency implementation; FNV-1a + MurmurHash3 finalizer for hash.
// Private to the metalog module — not part of the public API.

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string_view>

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
        double sum = 0.0;
        int zeros = 0;
        for (auto reg : regs_)
        {
            sum += std::ldexp(1.0, -static_cast<int>(reg));
            if (reg == 0)
                ++zeros;
        }
        constexpr double alpha = 0.7213 / (1.0 + (1.079 / static_cast<double>(kNumRegisters)));
        const double raw =
            alpha * static_cast<double>(kNumRegisters) * static_cast<double>(kNumRegisters) / sum;

        // Small-range correction (linear counting).
        constexpr double kHllSmallRangeCorrection{2.5};
        if (zeros > 0 && raw < kHllSmallRangeCorrection * static_cast<double>(kNumRegisters))
            return static_cast<std::uint64_t>(
                static_cast<double>(kNumRegisters) *
                std::log(static_cast<double>(kNumRegisters) / static_cast<double>(zeros)));

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
