// Unit tests: allow short identifiers and test-specific patterns
//
// test_stability_vs_diff_divergence.cpp — the RETENTION CUT is what separates a document's
// `stability` block from `diff()`'s divergence, and this file is the witness that says so.
//
// ── THE TWO READERS OF ONE QUANTITY
//
// `js_divergence` is computed twice in this package, from two different populations:
//
//   producer side  `MetaLogEngine::build_stability` reads the WINDOW — every accumulated bucket
//                  and `lines_observed`, before any cap is applied. `prev_freq_` on the other
//                  side of the comparison is likewise the previous window's FULL bucket set.
//   operations side `diff(previous, current)` reads the two DOCUMENTS — `stats.top_k` only,
//                  which the producer already truncated at `min(top_k_size, unique_templates)`.
//
// Below the cut those two populations are the same set, so the two numbers are the same number.
// Above it they are not, and the whole point of `stability` being a separate block is that it
// keeps reading the full window when the document no longer carries it.
//
// ── WHY THIS FILE EXISTS AT ALL: THE GREEN THAT SAID NOTHING
//
// The equality half was already true and already observable — every committed golden vector in
// `tests/vectors/` shows `stability.js_divergence` equal to line 3's `js_divergence`, bit for bit,
// on all three corpora. It was true VACUOUSLY: every one of those windows carries between 1 and 12
// unique templates against the default `top_k_size` of 64, so `stats.top_k` is never truncated and
// the two populations cannot differ. An equality asserted only in the regime where it is forced is
// not an assertion. `DN-50.D8` predicted exactly that in writing — *"a gate that only asserts
// equality would be vacuously green on every small fixture"* — and this file is the half it asked
// for, produced by moving the cut instead of by adding a corpus.
//
// ── HOW THE INFORMATIVE CASE IS REACHED: THE KNOB, NOT A NEW FIXTURE
//
// `MetaLogConfig::top_k_size` is a producer knob, so the truncation regime is one config member
// away on the corpus already in the tree. The two arms below run the SAME committed log bytes
// through the SAME shared tokenize-and-split construction and differ in that member alone — a
// do-operator on the retention cut and on nothing else. That is why arm 3 can attribute the
// separation to the cut rather than to the input.
//
// The premise each arm needs is ASSERTED rather than assumed: arm 1 asserts that nothing was
// truncated, arm 2 asserts that both windows were. Should the corpus ever stop having the shape
// these arms need, they RED with the observed counts in the message instead of going quietly
// vacuous the way the equality-only reading did.
//
// ── EXACT COMPARISON, AND WHY NO TOLERANCE IS THE RIGHT ANSWER
//
// `divergences()` accumulates in the integer/fixed-point domain (`__int128` reductions over
// `det_log2_fixed`, one exact divide per output — src/stats/statistics.cpp) precisely so the
// result carries no accumulation order and no libm dependence. Two calls over the same population
// therefore produce the same bits on every leg, and the comparison here is `==` on the doubles.
// A tolerance would be strictly worse than useless: the drift it would absorb is the drift this
// gate exists to see.
//
// ── WHAT A FUTURE CHANGE WOULD HAVE TO DO TO RED THIS FILE
//
//   * `build_stability` reusing `WindowAnalysis::ordered[0 .. top_k_cut]` instead of the full
//     bucket set — the plausible "reuse the sorted view we already built" optimisation. Arm 2 and
//     arm 3 both red: the producer number would start following the knob.
//   * `diff()` reading a full-template array instead of `stats.top_k` — arm 2 and arm 3 red from
//     the other side.
//   * The corpus losing its 9 -> 12 unique-template shape — the premise assertions red by name.
//
// Determinism: no RNG, no threads, no wall clock. The window bounds are the literal epoch offsets
// owned by `corpus_windows_scenario.hpp`; the input is committed log text; the divergence is
// integer-domain. Single-threaded by construction.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import insight.metalog.test;

// AFTER the imports (plain TU): the corpus tokenize-and-split construction shared with
// scripts/determinism_fixture.cpp and tests/operations/test_golden_vectors.cpp.
#include "corpus_windows_scenario.hpp"

namespace
{

namespace meta = insight::metalog;
namespace cw = insight::metalog::corpus_windows;

// The corpus with the richest split in the tree: 9 unique templates in the first window, 12 in the
// second (re-derived from tests/vectors/service_a.vectors.jsonl). It is the only committed corpus
// whose window count is high enough for a small cut to bite on BOTH sides of the diff.
constexpr std::string_view kCorpusName{"service_a"};

// Two coordinates on the one axis this file sweeps.
//   - the shipped default: 64 >= 12, so nothing is truncated and the populations coincide.
//   - a cut of 2: below both windows' template counts, so both documents lose entries.
// 2 rather than 1 so the retained set is still a set and the truncation is not confounded with a
// degenerate single-entry document.
constexpr std::size_t kNoTruncationTopK{meta::MetaLogConfig::kDefaultTopKSize};
constexpr std::size_t kTruncatingTopK{2};

// Every number a failure needs, gathered in one pass so the arms never re-run the producer and
// then compare readings from two different runs.
struct Measurement
{
    std::size_t configured_top_k{0};
    std::uint64_t previous_unique{0};
    std::uint64_t current_unique{0};
    std::size_t previous_retained{0};
    std::size_t current_retained{0};
    double stability_js{0.0};
    double diff_js{0.0};
};

[[nodiscard]] std::filesystem::path corpus_path()
{
    return std::filesystem::path{INSIGHT_METALOG_CORPUS_DIR} / (std::string{kCorpusName} + ".log");
}

// `std::nullopt` distinguishes the two ways this can fail to produce a reading — an unreadable
// corpus, and a document that carried no divergence at all — from a reading that merely came out
// wrong. The caller reports which.
enum class ProduceFailure : std::uint8_t
{
    CorpusUnreadable,
    StabilityBlockAbsent,
    DiffDivergenceAbsent
};

[[nodiscard]] std::expected<Measurement, ProduceFailure> measure(std::size_t top_k_size)
{
    const auto lines{cw::read_lines(corpus_path().string())};
    if (!lines)
        return std::unexpected{ProduceFailure::CorpusUnreadable};

    meta::MetaLogConfig config;
    cw::configure(config);
    config.top_k_size = top_k_size;

    meta::MetaLogEngine engine{config};
    const auto pair{cw::build(engine, *lines)};
    const auto diffed{meta::diff(pair.previous, pair.current)};

    if (!pair.current.stability.has_value())
        return std::unexpected{ProduceFailure::StabilityBlockAbsent};
    if (!diffed.js_divergence.has_value())
        return std::unexpected{ProduceFailure::DiffDivergenceAbsent};

    return Measurement{.configured_top_k = top_k_size,
                       .previous_unique = pair.previous.stats.unique_templates,
                       .current_unique = pair.current.stats.unique_templates,
                       .previous_retained = pair.previous.stats.top_k.size(),
                       .current_retained = pair.current.stats.top_k.size(),
                       .stability_js = pair.current.stability->js_divergence,
                       .diff_js = *diffed.js_divergence};
}

[[nodiscard]] std::string_view describe(ProduceFailure failure)
{
    switch (failure)
    {
    case ProduceFailure::CorpusUnreadable:
        return "the corpus file could not be opened";
    case ProduceFailure::StabilityBlockAbsent:
        return "the second window emitted no stability block (MetaLogConfig::emit_stability, or "
               "an empty window)";
    case ProduceFailure::DiffDivergenceAbsent:
        return "diff() carried no js_divergence (one side observed zero lines)";
    }
    return "<unknown failure>";
}

// 17 significant digits round-trips an IEEE-754 double, so a reader comparing two reports is
// comparing the values and not their rendering.
[[nodiscard]] std::string report(const Measurement& measurement)
{
    return std::format("\n  corpus                        {}"
                       "\n  configured top_k_size         {}"
                       "\n  previous window  unique={}  retained in stats.top_k={}"
                       "\n  current  window  unique={}  retained in stats.top_k={}"
                       "\n  stability.js_divergence       {:.17g}   (read over the FULL window)"
                       "\n  diff().js_divergence          {:.17g}   (read over stats.top_k only)"
                       "\n  difference                    {:.17g}",
                       kCorpusName, measurement.configured_top_k, measurement.previous_unique,
                       measurement.previous_retained, measurement.current_unique,
                       measurement.current_retained, measurement.stability_js, measurement.diff_js,
                       measurement.stability_js - measurement.diff_js);
}

// ── Arm 1 — below the cut: the two readings are the same number, bit for bit ───────────────────
//
// This is the half that was already green. It stays, because it is the control that makes arm 2
// mean something — but its premise is now asserted, so it can no longer be green for the reason
// that made it uninformative.
TEST(StabilityVersusDiffDivergence, BelowTheRetentionCutTheTwoDivergencesAreBitEqual)
{
    const auto measured{measure(kNoTruncationTopK)};
    ASSERT_TRUE(measured.has_value()) << describe(measured.error()) << " (" << corpus_path() << ")";

    ASSERT_EQ(measured->previous_retained, measured->previous_unique)
        << "PREMISE BROKEN: this arm only says something while the previous window is retained "
           "WHOLE. It is not."
        << report(*measured);
    ASSERT_EQ(measured->current_retained, measured->current_unique)
        << "PREMISE BROKEN: this arm only says something while the current window is retained "
           "WHOLE. It is not."
        << report(*measured);

    EXPECT_EQ(measured->stability_js, measured->diff_js)
        << "With nothing truncated, the producer and diff() read the SAME template population, so "
           "their Jensen-Shannon divergences must be identical bits — the reduction is "
           "integer-domain and carries no rounding freedom."
        << report(*measured);
}

// ── Arm 2 — above the cut: the two readings separate ───────────────────────────────────────────
//
// The informative half, and the one that had zero witnesses before this file.
TEST(StabilityVersusDiffDivergence, AboveTheRetentionCutTheTwoDivergencesSeparate)
{
    const auto measured{measure(kTruncatingTopK)};
    ASSERT_TRUE(measured.has_value()) << describe(measured.error()) << " (" << corpus_path() << ")";

    ASSERT_GT(measured->previous_unique, kTruncatingTopK)
        << "PREMISE BROKEN: the previous window does not exceed the cut, so nothing is truncated "
           "on that side and this arm would be vacuous."
        << report(*measured);
    ASSERT_GT(measured->current_unique, kTruncatingTopK)
        << "PREMISE BROKEN: the current window does not exceed the cut, so nothing is truncated "
           "on that side and this arm would be vacuous."
        << report(*measured);
    ASSERT_EQ(measured->previous_retained, kTruncatingTopK)
        << "PREMISE BROKEN: the previous document did not truncate at the configured cut."
        << report(*measured);
    ASSERT_EQ(measured->current_retained, kTruncatingTopK)
        << "PREMISE BROKEN: the current document did not truncate at the configured cut."
        << report(*measured);

    EXPECT_NE(measured->stability_js, measured->diff_js)
        << "Above the cut the two readings MUST part: the producer still sees every template in "
           "the window, diff() sees only the entries that survived into stats.top_k. Equality here "
           "means one of the two has started reading the other's population — most likely "
           "build_stability reusing the already-sorted top_k view."
        << report(*measured);
}

// ── Arm 3 — WHICH of the two follows the knob ──────────────────────────────────────────────────
//
// Arm 2 on its own says only "two numbers differ", which a wrong change could satisfy by moving
// EITHER of them. This arm names the direction: sweeping `top_k_size` must move the document-side
// reading and must leave the window-side reading untouched. That is the design fact `DN-50.D1`(2)
// established — `stability` is not the acute series — stated as something the code has to hold.
TEST(StabilityVersusDiffDivergence, StabilityIgnoresTheRetentionCutAndTheDiffFollowsIt)
{
    const auto whole{measure(kNoTruncationTopK)};
    ASSERT_TRUE(whole.has_value()) << describe(whole.error()) << " (" << corpus_path() << ")";
    const auto truncated{measure(kTruncatingTopK)};
    ASSERT_TRUE(truncated.has_value())
        << describe(truncated.error()) << " (" << corpus_path() << ")";

    ASSERT_EQ(whole->current_retained, whole->current_unique)
        << "PREMISE BROKEN: the untruncated leg truncated." << report(*whole);
    ASSERT_LT(truncated->current_retained, truncated->current_unique)
        << "PREMISE BROKEN: the truncated leg did not truncate." << report(*truncated);

    EXPECT_EQ(whole->stability_js, truncated->stability_js)
        << "stability.js_divergence is a property of the WINDOW and must not move when the "
           "retention cut moves. It moved, so the producer is now reading a capped population — "
           "the document's summary has become a function of how much of the window the document "
           "kept, which is the confusion the separate stability block exists to prevent."
        << "\n\n[untruncated leg]" << report(*whole) << "\n\n[truncated leg]" << report(*truncated);

    EXPECT_NE(whole->diff_js, truncated->diff_js)
        << "diff().js_divergence is a property of the two DOCUMENTS and must move when the cut "
           "moves, because the cut is what decides which templates those documents carry. It did "
           "not move, so diff() is reading something other than stats.top_k."
        << "\n\n[untruncated leg]" << report(*whole) << "\n\n[truncated leg]" << report(*truncated);
}

} // namespace
