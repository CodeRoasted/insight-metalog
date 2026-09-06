
// refs: DN-50.D1, DN-50.D8
// invariant: the RETENTION CUT is what separates a document's stability block from diff()'s
// divergence, and this file is the witness that says so.
// invariant: js_divergence is computed twice from two populations -- the producer reads the WINDOW
// before any cap, and diff() reads the two DOCUMENTS' already-truncated top_k.
// invariant: below the cut those populations are the same set and the two numbers are the same
// number; above it they are not, and that is why stability is a separate block.
// invariant: the equality half was already true and observable, but VACUOUSLY: every committed
// window carries 1 to 12 unique templates against a default top_k_size of 64.
// note: nothing is truncated there, and an equality forced by the regime is not an assertion.
// invariant: the informative case is reached by the KNOB and not a new fixture: the same committed
// bytes through the same construction, differing in top_k_size alone -- a do-operator on the cut.
// invariant: each arm's premise is ASSERTED rather than assumed, so a corpus that stops having the
// needed shape REDS with the observed counts instead of going quietly vacuous.
// invariant: the comparison is == on the doubles because divergences() accumulates in the
// integer/fixed-point domain, so two calls over one population produce the same bits on every leg.
// note: a tolerance would absorb exactly the drift this gate exists to see.
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

// note: after the imports, plain TU: the construction shared with the fixture and the vectors.
#include "corpus_windows_scenario.hpp"

namespace
{

namespace meta = insight::metalog;
namespace cw = insight::metalog::corpus_windows;

// invariant: the corpus with the richest split in the tree -- 9 unique templates in the first
// window and 12 in the second -- and the only one whose counts let a small cut bite on BOTH sides.
constexpr std::string_view kCorpusName{"service_a"};

// invariant: two coordinates on the one axis swept: the shipped default of 64, which truncates
// nothing, and a cut of 2, which is below both windows' template counts.
// note: 2 rather than 1, so the retained set is still a set and truncation is not confounded.
constexpr std::size_t kNoTruncationTopK{meta::MetaLogConfig::kDefaultTopKSize};
constexpr std::size_t kTruncatingTopK{2};

// invariant: every number a failure needs is gathered in one pass, so the arms never re-run the
// producer and then compare readings from two different runs.
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

// invariant: the disengaged optional distinguishes an unreadable corpus and a document carrying no
// divergence from a reading that merely came out wrong, and the caller reports which.
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

// note: 17 significant digits round-trips an IEEE-754 double, so a reader compares values.
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

// invariant: arm 1, below the cut -- the half that was already green, kept because it is the
// control that makes arm 2 mean something, and its premise is now asserted.
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

// invariant: arm 2, above the cut -- the informative half, and the one that had zero witnesses.
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

// invariant: arm 3 names the DIRECTION, because arm 2 alone says only that two numbers differ,
// which a wrong change could satisfy by moving either of them.
// invariant: sweeping top_k_size must move the document-side reading and leave the window-side
// reading untouched -- stability is not the acute series, stated as something the code holds.
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
