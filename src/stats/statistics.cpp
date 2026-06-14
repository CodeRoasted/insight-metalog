module;

module insight.metalog.detail.stats;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;

namespace insight::metalog
{

namespace
{
// Jensen-Shannon is the average of the two directional KL terms: js = ½(D_p + D_q).
constexpr double kJsSymmetryFactor{0.5};

// 128-bit accumulators come from canon's det shim: native `__int128` on gcc/clang, a portable
// constexpr struct on MSVC (which has no __int128). Same two's-complement semantics, so the digest
// is bit-identical cross-OS. as_i128 widens a u64 count VALUE-PRESERVING (every u64 is a
// non-negative i128) — matching native widening of a u64, NOT via int64 (which sign-flips
// ≥2^63). [[msvc-port-stdlib-isms]]
using i128 = insight::det::i128;
[[nodiscard]] constexpr i128 as_i128(std::uint64_t value) noexcept
{
    return static_cast<i128>(insight::det::u128{value});
}
} // namespace

double shannon_entropy_bits(const std::vector<std::uint64_t>& counts, std::uint64_t total)
{
    if (total == 0)
        return 0.0;
    // H = (Σ cᵢ·(log2(total) − log2(cᵢ))) / total, accumulated as an
    // exact __int128 over the integer/count domain (no libm, order-independent),
    // converted to double by one exact divide. Deterministic cross-machine.
    insight::det::FixedReducer reducer;
    const std::int64_t log2_total{insight::det::det_log2_fixed(total)};
    for (auto count : counts)
    {
        if (count == 0)
            continue;
        reducer.add_fixed(as_i128(count) *
                          (log2_total - insight::det::det_log2_fixed(count)));
    }
    return reducer.normalized_bits(static_cast<std::int64_t>(total));
}

DivergenceResult divergences(const std::unordered_map<std::string, std::uint64_t>& cur,
                             std::uint64_t cur_total,
                             const std::unordered_map<std::string, std::uint64_t>& prev,
                             std::uint64_t prev_total)
{
    // Union of keys, in a defined (sorted) order. Pointers into the maps
    // (no string copies). Integer accumulation below is order-invariant anyway,
    // but the explicit order keeps the contract clear and avoids iterating an
    // unordered container for output.
    std::vector<const std::string*> keys;
    keys.reserve(cur.size() + prev.size());
    for (const auto& entry : cur)
        keys.push_back(&entry.first);
    for (const auto& entry : prev)
        if (!cur.contains(entry.first))
            keys.push_back(&entry.first);
    if (keys.empty() || cur_total == 0 || prev_total == 0)
        return {.kl = 0.0, .js = 0.0};
    std::ranges::sort(keys,
                      [](const std::string* lhs, const std::string* rhs) { return *lhs < *rhs; });

    // Laplace smoothing (alpha = 1): with key_count = |union|, the smoothed
    // frequencies p = (cn+1)/cur_denom and q = (pn+1)/prev_denom are ratios of
    // INTEGERS (cur_denom = cur_total + key_count, prev_denom = prev_total +
    // key_count are integers), so every log2 is a det_log2_fixed difference and the
    // reductions stay exact in the integer/fixed-point domain. One exact
    // divide per output.
    //   KL = (1/cur_denom)·Σ (cn+1)·[log2((cn+1)·prev_denom) − log2((pn+1)·cur_denom)]
    //   JS = ½·[ (1/cur_denom)·Σ(cn+1)·L_p + (1/prev_denom)·Σ(pn+1)·L_q ], where
    //        D   = (cn+1)·prev_denom + (pn+1)·cur_denom   (= 2·cur_denom·prev_denom·m)
    //        L_p = log2(2·(cn+1)·prev_denom) − log2(D)     (= log2(p/m))
    //        L_q = log2(2·(pn+1)·cur_denom)  − log2(D)     (= log2(q/m))
    const std::uint64_t key_count{keys.size()};
    const std::uint64_t cur_denom{cur_total + key_count};
    const std::uint64_t prev_denom{prev_total + key_count};

    i128 kl_acc{0};
    i128 js_p_acc{0};
    i128 js_q_acc{0};
    for (const std::string* keyp : keys)
    {
        const auto cur_it{cur.find(*keyp)};
        const auto prev_it{prev.find(*keyp)};
        const std::uint64_t pnum{(cur_it == cur.end() ? 0U : cur_it->second) + 1U};    // cn + alpha
        const std::uint64_t qnum{(prev_it == prev.end() ? 0U : prev_it->second) + 1U}; // pn + alpha
        const std::uint64_t p_arg{pnum * prev_denom};
        const std::uint64_t q_arg{qnum * cur_denom};
        const std::uint64_t divergence_d{p_arg + q_arg};
        const std::int64_t log2_d{insight::det::det_log2_fixed(divergence_d)};
        kl_acc += as_i128(pnum) *
                  (insight::det::det_log2_fixed(p_arg) - insight::det::det_log2_fixed(q_arg));
        js_p_acc +=
            as_i128(pnum) * (insight::det::det_log2_fixed(2U * p_arg) - log2_d);
        js_q_acc +=
            as_i128(qnum) * (insight::det::det_log2_fixed(2U * q_arg) - log2_d);
    }
    double kl_value{insight::det::fixed_to_double(
        insight::det::round_div(kl_acc, static_cast<std::int64_t>(cur_denom)))};
    const std::int64_t js_p_q{
        insight::det::round_div(js_p_acc, static_cast<std::int64_t>(cur_denom))};
    const std::int64_t js_q_q{
        insight::det::round_div(js_q_acc, static_cast<std::int64_t>(prev_denom))};
    double js_value{kJsSymmetryFactor * insight::det::fixed_to_double(js_p_q + js_q_q)};
    // NOLINTNEXTLINE(readability-use-std-min-max)
    if (kl_value < 0.0)
        kl_value = 0.0;
    js_value = std::clamp(js_value, 0.0, 1.0);
    return {.kl = kl_value, .js = js_value};
}

std::pair<std::uint64_t, std::uint64_t>
new_and_vanished(const std::unordered_map<std::string, std::uint64_t>& cur,
                 const std::unordered_map<std::string, std::uint64_t>& prev)
{
    std::uint64_t added = 0;
    std::uint64_t gone = 0;
    for (const auto& entry : cur)
        if (!prev.contains(entry.first))
            ++added;
    for (const auto& entry : prev)
        if (!cur.contains(entry.first))
            ++gone;
    return {added, gone};
}

double histogram_js(const std::unordered_map<std::string, std::uint64_t>& prev,
                    std::uint64_t prev_total,
                    const std::unordered_map<std::string, std::uint64_t>& curr,
                    std::uint64_t curr_total)
{
    if (prev_total == 0 || curr_total == 0)
        return 0.0;

    // Union keys in sorted order; integer-domain JS via det_log2_fixed,
    // identical Laplace-smoothed convention as divergences().
    std::vector<const std::string*> keys;
    keys.reserve(prev.size() + curr.size());
    for (const auto& [key, _sink] : prev)
        keys.push_back(&key);
    for (const auto& [key, _sink] : curr)
        if (!prev.contains(key))
            keys.push_back(&key);
    if (keys.empty())
        return 0.0;
    std::ranges::sort(keys,
                      [](const std::string* lhs, const std::string* rhs) { return *lhs < *rhs; });

    const std::uint64_t key_count{keys.size()};
    const std::uint64_t p_denom{prev_total + key_count};
    const std::uint64_t c_denom{curr_total + key_count};

    // js = ½·[ (1/p_denom)·Σ(pn+1)·L_p + (1/c_denom)·Σ(cn+1)·L_c ], with
    //   D   = (pn+1)·c_denom + (cn+1)·p_denom
    //   L_p = log2(2·(pn+1)·c_denom) − log2(D)
    //   L_c = log2(2·(cn+1)·p_denom) − log2(D)
    i128 prev_acc{0};
    i128 curr_acc{0};
    for (const std::string* keyp : keys)
    {
        const auto p_it{prev.find(*keyp)};
        const auto c_it{curr.find(*keyp)};
        const std::uint64_t pnum{(p_it == prev.end() ? 0U : p_it->second) + 1U};
        const std::uint64_t cnum{(c_it == curr.end() ? 0U : c_it->second) + 1U};
        const std::uint64_t p_arg{pnum * c_denom};
        const std::uint64_t c_arg{cnum * p_denom};
        const std::int64_t log2_d{insight::det::det_log2_fixed(p_arg + c_arg)};
        prev_acc +=
            as_i128(pnum) * (insight::det::det_log2_fixed(2U * p_arg) - log2_d);
        curr_acc +=
            as_i128(cnum) * (insight::det::det_log2_fixed(2U * c_arg) - log2_d);
    }
    const std::int64_t prev_q{
        insight::det::round_div(prev_acc, static_cast<std::int64_t>(p_denom))};
    const std::int64_t curr_q{
        insight::det::round_div(curr_acc, static_cast<std::int64_t>(c_denom))};
    const double js_value{kJsSymmetryFactor * insight::det::fixed_to_double(prev_q + curr_q)};
    return std::clamp(js_value, 0.0, 1.0);
}

} // namespace insight::metalog
