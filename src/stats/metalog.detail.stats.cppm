// insight.metalog.detail.stats — SEALED statistics domain (domain decomposition, §11.9.11).
// HLL cardinality sketch, distribution statistics (entropy/KL/JS), salience scoring, and
// wire-format helpers. A leaf over api+canon — imports no other metalog detail shard.
// Never re-exported by the facade and never installed (PRIVATE file set).
export module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

export namespace insight::metalog
{

// ── HyperLogLog ───────────────────────────────────────────────────────────────
// Sketch with p=14 (m=16384 registers, ~1.5% standard error).
// Zero-dependency implementation; FNV-1a + MurmurHash3 finalizer for hash.
// Private to the metalog module — not part of the public API.

class HyperLogLog
{
  public:
    static constexpr std::uint8_t kPrecision{14};
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
        // The harmonic sum Σ 2^(−rⱼ) is an EXACT dyadic fixed-point sum.
        // Registers are integers in [0, 64−kPrecision+1] = [0, 51], so each term
        // 2^(−reg) is an exact power of two; accumulating 2^(kHllFrac−reg) in
        // __int128 has no rounding and no order dependence (unlike the prior
        // double Σ std::ldexp, which dropped low-order terms). No libm.
        constexpr int kHllFrac{52}; // > max register value (51) → every term exact
        // det::u128 (canon shim: native unsigned __int128 on gcc/clang, portable struct on MSVC).
        insight::det::u128 sum_fixed{0};
        int zeros{0};
        for (auto reg : regs_)
        {
            sum_fixed += insight::det::u128{1}
                         << static_cast<unsigned>(kHllFrac - static_cast<int>(reg));
            if (reg == 0)
                ++zeros;
        }

        // raw = α·m²/S = (α·m²·2^kHllFrac) / S_fixed. The numerator is α's 53-bit mantissa scaled
        // by exact powers of two → an EXACT, compiler-identical double whose value is an INTEGER
        // (mantissa × 2^k, k ≥ 0). We need it as u128. A hardware double→int128 cast is forbidden
        // in deterministic content (CLAUDE.md: the float→int instruction can diverge across ISAs),
        // so convert by INTEGER bit-extraction of the IEEE-754 representation — pure shifts,
        // bit-identical everywhere, and exactly equal to truncating the (integer-valued) double.
        // All compile-time.
        constexpr double kAlpha{0.7213 / (1.0 + (1.079 / static_cast<double>(kNumRegisters)))};
        constexpr double kNumerator{kAlpha * static_cast<double>(kNumRegisters) *
                                    static_cast<double>(kNumRegisters) *
                                    static_cast<double>(std::uint64_t{1} << kHllFrac)};
        constexpr std::uint64_t kNumBits{std::bit_cast<std::uint64_t>(kNumerator)};
        constexpr std::uint64_t kMantissaMask{(std::uint64_t{1} << 52U) - 1U};
        constexpr std::uint64_t kSignificand{(kNumBits & kMantissaMask) |
                                             (std::uint64_t{1} << 52U)};
        constexpr int kNumExp{static_cast<int>((kNumBits >> 52U) & 0x7FFU) - 1023 - 52};
        static_assert(kNumExp >= 0,
                      "HLL numerator must be integer-valued (significand << k, k >= 0)");
        const insight::det::u128 raw_numerator{insight::det::u128{kSignificand}
                                               << static_cast<unsigned>(kNumExp)};
        // sum_fixed > 0 always: regs_ is a fixed, compile-time-non-empty std::array
        // (kNumRegisters > 0) and every term 1<<(kHllFrac-reg) is >= 2, so the loop
        // above accumulates a strictly positive sum. The analyzer cannot prove the
        // range-for executes; this divide is never by zero.
        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        const insight::det::u128 raw{raw_numerator / sum_fixed};

        // Small-range correction (linear counting): m·ln(m/zeros), via det_ln.
        constexpr std::uint64_t kSmallRangeThreshold{(5U * kNumRegisters) / 2U}; // 2.5·m
        if (zeros > 0 && raw < kSmallRangeThreshold)
        {
            // ln(m/zeros) = ln(m) − ln(zeros) ≥ 0 (m ≥ zeros), in Qk; m·(…) then >> kFracBits.
            // u128 (canon shim) — the value is non-negative, so a logical >> equals the old
            // __int128 arithmetic >> exactly, and u128 carries operator>> (no i128 shift needed).
            const std::int64_t ln_diff{
                insight::det::det_ln_fixed(kNumRegisters) -
                insight::det::det_ln_fixed(static_cast<std::uint64_t>(zeros))};
            const insight::det::u128 linear{
                insight::det::u128{kNumRegisters} *
                insight::det::u128{static_cast<std::uint64_t>(ln_diff)}};
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

// ── Distribution statistics ───────────────────────────────────────────────────
// Deterministic distribution statistics over template/value frequency maps:
// Shannon entropy and the KL / Jensen-Shannon divergences.
// Integer/fixed-point math (via insight::det); cross-machine bit-identical.
// Shared by the engine (close_window stability + entropies), compose, and diff.

// Shannon entropy in bits over a (possibly partial) frequency
// distribution: -Σ p log2 p. Computed over the bucketed templates
// we have full counts for; tail templates we only know the sum
// of, so we treat them collectively as a single residual bucket.
[[nodiscard]] double shannon_entropy_bits(const std::vector<std::uint64_t>& counts,
                                          std::uint64_t total);

// KL(p || q) over the union of keys, with Laplace add-α smoothing
// applied to BOTH distributions so that zero entries on either
// side don't blow up to infinity. α = 1 over the union size is a
// standard cheap choice; the resulting divergence is biased but
// bounded and monotone in the underlying distributional change.
struct DivergenceResult
{
    double kl;
    double js;
};

[[nodiscard]] DivergenceResult
divergences(const std::unordered_map<TemplateId, std::uint64_t>& cur, std::uint64_t cur_total,
            const std::unordered_map<TemplateId, std::uint64_t>& prev, std::uint64_t prev_total);

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t>
new_and_vanished(const std::unordered_map<TemplateId, std::uint64_t>& cur,
                 const std::unordered_map<TemplateId, std::uint64_t>& prev);

// JS divergence between two per-param value-count maps.
//
// Uses the same Laplace-smoothed log2 convention as divergences():
//   alpha = 1, smoothed over the union of keys.
// Returns value in [0, 1] (bits, clamped).
// Returns 0.0 when either total is zero.
[[nodiscard]] double histogram_js(const std::unordered_map<std::string, std::uint64_t>& prev,
                                  std::uint64_t prev_total,
                                  const std::unordered_map<std::string, std::uint64_t>& curr,
                                  std::uint64_t curr_total);

// ── Salience scoring ──────────────────────────────────────────────────────────
// Turning a template's level, structural role, structural-surprise and
// self-novelty into a deterministic, integer-quantized salience used to admit
// rare-but-meaningful templates into the reservoir.
// Integer math only — no float (I5). Shared by engine and compose.

// Most-frequent level / structural role for a template (argmax over the count
// map). Roles are deterministic per template, so dominant_role_of is "the" role.
[[nodiscard]] std::optional<LogLevel>
dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels);
[[nodiscard]] StructuralRole
dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles);

// Most-frequent component for a template (argmax over the count map; ties broken by
// component string asc — a pure function of the contents, stdlib-identical). Empty
// string when the template carried no component. Feeds the §16.6 reservoir→cell cross
// (the WHERE-path of a salient template's LOCATION).
[[nodiscard]] std::string
dominant_component_of(const std::unordered_map<std::string, std::uint64_t>& components);

// Structural-surprise band (0..100) for the MOST-LIKELY incoming edge p = c/t into
// a template: high only when even a template's easiest way in is rare. Integer
// thresholds on c·K vs t (no float, I5); c==0 means root/unreachable (not surprising).
[[nodiscard]] std::uint32_t surprise_band(std::uint64_t edge_count,
                                          std::uint64_t source_outgoing) noexcept;

// Self-novelty band (0..100): how late a template first appeared within the window
// (first-seen ordinal over line count). Self-relative (I3); integer-only (I5).
[[nodiscard]] std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                                         std::uint64_t count) noexcept;

// Deterministic, quantized salience: (severity ⊕ structural_surprise ⊕ novelty) ⊗
// rarity. Returns 0 for a non-salient template (so rare-benign noise never enters
// the reservoir). Integer math only — no float (I5). `echoed_source` (D-PROV-1 §3.1):
// when true the LEVEL-BLIND failure-cue tier is skipped (an all-echoed `…failed…`
// template must not be re-promoted after A1 demoted its level to Unknown).
[[nodiscard]] std::uint32_t salience_score(std::optional<LogLevel> level, StructuralRole role,
                                           std::string_view tmpl, bool echoed_source,
                                           std::uint64_t count, std::uint64_t lines,
                                           std::uint32_t structural_surprise,
                                           std::uint32_t novelty) noexcept;

// ── Wire-format helpers ───────────────────────────────────────────────────────
// MetaLog wire-format helpers (spec §2/§3): rendering domain values into the
// exact strings the v0.6.0 envelope requires.

// RFC 3339 UTC, fixed widths, always trailing 'Z' (e.g. "2026-04-24T10:00:00Z").
[[nodiscard]] std::string format_rfc3339_utc(Timestamp timestamp);

// SPEC level string. UNKNOWN maps to INFO — the spec defines no UNKNOWN level.
[[nodiscard]] std::string level_to_spec_string(LogLevel level);

} // namespace insight::metalog
