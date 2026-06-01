#pragma once

// Deterministic distribution statistics over template/value frequency maps:
// Shannon entropy and the KL / Jensen-Shannon divergences. Single responsibility
// — the integer/fixed-point math (via insight::det) that turns count maps into
// the spec's bit fields. Cross-machine bit-identical (F5); shared by the engine
// (close_window stability + entropies), compose, and diff.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace insight::metalog::detail
{

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
divergences(const std::unordered_map<std::string, std::uint64_t>& cur, std::uint64_t cur_total,
            const std::unordered_map<std::string, std::uint64_t>& prev, std::uint64_t prev_total);

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t>
new_and_vanished(const std::unordered_map<std::string, std::uint64_t>& cur,
                 const std::unordered_map<std::string, std::uint64_t>& prev);

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

} // namespace insight::metalog::detail
