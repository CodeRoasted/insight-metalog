// refs: ADR-3.D4
export module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

export namespace insight::metalog
{

class HyperLogLog
{
  public:
    static constexpr std::uint8_t kPrecision{14};
    static constexpr std::size_t kNumRegisters{1U << kPrecision};

    HyperLogLog() noexcept
    {
        regs_.fill(0);
    }

    void add(std::string_view value) noexcept
    {
        const auto hash_val = hash64(value);
        const auto idx = static_cast<std::size_t>(hash_val >> (64U - kPrecision));
        const auto hash_rem = hash_val << kPrecision;
        const auto rho = static_cast<std::uint8_t>(
            (hash_rem == 0U) ? (64U - kPrecision + 1U) : (std::countl_zero(hash_rem) + 1U));
        // note: NOLINT: the conditional store is deliberate; std::max would store on every add.
        // NOLINTNEXTLINE(readability-use-std-min-max)
        if (rho > regs_[idx])
            regs_[idx] = rho;
    }

    [[nodiscard]] std::uint64_t estimate() const noexcept
    {
        // assert: every term 1 << (kHllFrac - reg) is exact -- reg <= 51 < kHllFrac -- so the sum
        // carries no rounding and no order dependence.
        constexpr int kHllFrac{52};
        insight::det::u128 sum_fixed{0};
        int zeros{0};
        for (auto reg : regs_)
        {
            sum_fixed += insight::det::u128{1}
                         << static_cast<unsigned>(kHllFrac - static_cast<int>(reg));
            if (reg == 0)
                ++zeros;
        }

        // note: the numerator reaches u128 by IEEE-754 bit extraction, never a float-to-int cast.
        // refs: BIB:determinism_model
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
        // assert: sum_fixed > 0 -- regs_ is a non-empty std::array and every term is at least 2.
        const insight::det::u128 raw{raw_numerator / sum_fixed};

        // note: the small-range arm is HyperLogLog's linear-counting correction, m*ln(m/zeros).
        constexpr std::uint64_t kSmallRangeThreshold{(5U * kNumRegisters) / 2U};
        if (zeros > 0 && raw < kSmallRangeThreshold)
        {
            // assert: ln_diff >= 0 because kNumRegisters >= zeros, so the logical shift equals an
            // arithmetic one.
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
        hash_acc ^= hash_acc >> kMixShift;
        hash_acc *= kMurmurFinalMix1;
        hash_acc ^= hash_acc >> kMixShift;
        hash_acc *= kMurmurFinalMix2;
        hash_acc ^= hash_acc >> kMixShift;
        return hash_acc;
    }
};

// pre: `counts` may cover only part of the population; `total` is the full count.
// post: Shannon entropy in bits, 0.0 when total is 0; unlisted mass contributes nothing, so a
// partial `counts` under-states the entropy.
// post: computed over the integer domain via insight::det -- no libm, cross-machine bit-identical.
[[nodiscard]] double shannon_entropy_bits(const std::vector<std::uint64_t>& counts,
                                          std::uint64_t total);

struct DivergenceResult
{
    double kl;
    double js;
};

// post: KL(p || q) and JS over the union of keys, Laplace-smoothed with alpha = 1 on both sides so
// a zero on either side stays finite; kl >= 0 and js in [0, 1].
[[nodiscard]] DivergenceResult
divergences(const std::unordered_map<TemplateId, std::uint64_t>& cur, std::uint64_t cur_total,
            const std::unordered_map<TemplateId, std::uint64_t>& prev, std::uint64_t prev_total);

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t>
new_and_vanished(const std::unordered_map<TemplateId, std::uint64_t>& cur,
                 const std::unordered_map<TemplateId, std::uint64_t>& prev);

// post: JS in [0, 1] bits under the same Laplace convention as divergences; 0.0 when either total
// is zero.
[[nodiscard]] double histogram_js(const std::unordered_map<std::string, std::uint64_t>& prev,
                                  std::uint64_t prev_total,
                                  const std::unordered_map<std::string, std::uint64_t>& curr,
                                  std::uint64_t curr_total);

// post: the argmax level over the count map, or nullopt when the map is empty.
[[nodiscard]] std::optional<LogLevel>
dominant_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels);
// post: the same argmax as dominant_level_of, marked declared iff at least one observation at the
// winning level came from canon's declared layer.
// refs: DN-32.D3
[[nodiscard]] std::optional<EventLevel>
dominant_event_level_of(const std::unordered_map<LogLevel, std::uint64_t>& levels,
                        const std::unordered_map<LogLevel, std::uint64_t>& declared_levels);
[[nodiscard]] StructuralRole
dominant_role_of(const std::unordered_map<StructuralRole, std::uint64_t>& roles);

// post: the argmax component with ties broken by string asc, so the result is stdlib-identical;
// empty when no component was observed.
[[nodiscard]] std::string
dominant_component_of(const std::unordered_map<std::string, std::uint64_t, TransparentStringHash,
                                               std::equal_to<>>& components);

// post: a band in 0..100 for the most-likely incoming edge; 0 for a root, an edge seen once or a
// common transition.
// note: the thresholds are integer cross-multiplications, edge_count * K < source_outgoing.
[[nodiscard]] std::uint32_t surprise_band(std::uint64_t edge_count,
                                          std::uint64_t source_outgoing) noexcept;

// post: a band in 0..100 from the first-seen ordinal over the window's line count; 0 when the
// template recurs fewer than twice or lines is 0.
[[nodiscard]] std::uint32_t novelty_band(std::uint64_t first_seen_index, std::uint64_t lines,
                                         std::uint64_t count) noexcept;

// invariant: axis is engaged iff score > 0 -- a template with no salient axis has no argmax.
// refs: DN-64.D3
struct SalienceVerdict
{
    std::uint32_t score{0};
    std::optional<RetentionAxis> axis;
};

// post: score 0 and no axis for a non-salient template, so rare-benign noise never enters the
// reservoir.
// refs: SRC-D-PROV-1
[[nodiscard]] SalienceVerdict salience_score(std::optional<LogLevel> level, StructuralRole role,
                                             std::string_view tmpl, bool echoed_source,
                                             std::uint64_t count, std::uint64_t lines,
                                             std::uint32_t structural_surprise,
                                             std::uint32_t novelty) noexcept;

// post: RFC 3339 UTC with fixed field widths and a trailing 'Z'.
[[nodiscard]] std::string format_rfc3339_utc(Timestamp timestamp);

// post: total over LogLevel; Unknown renders as its own token, never an omission.
// note: an absent cube axis already means AGGREGATED, so a coord needs a token; a row omits.
// refs: DN-43.D10
[[nodiscard]] std::string level_to_spec_string(LogLevel level);

// post: nullopt when no level was observed -- a disengaged optional or EventLevel{} -- so the wire
// member is omitted.
// note: the producer has two spellings of no-level-observed; the wire has one, and it omits.
[[nodiscard]] std::optional<std::string> spec_level_of(const std::optional<EventLevel>& level);

// post: the spec's lower-case token, and nullopt for Unknown, which the wire spells by omission.
// note: deliberately not insight::to_string(RunOutcome) -- Sift's report wire is upper-case.
[[nodiscard]] std::optional<std::string> spec_run_outcome_of(RunOutcome outcome);

} // namespace insight::metalog
