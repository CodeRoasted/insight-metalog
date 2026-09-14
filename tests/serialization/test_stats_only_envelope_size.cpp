// invariant: the arm measures a stats-only document (no reservoir, behavior or cube) at top_k_size
// 32 inline over one window of 1 M lines, and pins its bytes.
// invariant: the engine always emits the cube and the acquisition block, so the measured document
// is the closed window with those two cleared; every other block is off by configuration.
// invariant: the stream has no parameters (no param_histograms), one level and a full top_k of 32.
// invariant: the counts are an integer harmonic series and the epoch is fixed, so the bytes are one
// number on every run and on both toolchains.
// refs: F-SRC-metalog-spec:SPEC.md
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

constexpr std::uint64_t kLines{1'000'000};
constexpr std::size_t kTemplates{64};
constexpr std::size_t kTopK{32};
constexpr std::uint64_t kHarmonicNumerator{210'000};
constexpr std::chrono::system_clock::time_point kEpoch{std::chrono::seconds{1'700'000'000}};

// invariant: a CHARACTERIZATION pin, measured by this arm and never guessed; a move is a change to
// the producer's bytes, re-measured and never refitted.
constexpr std::size_t kStatsOnlyBytes{6'113};
constexpr std::string_view kInlineTemplateKey{"\"template\":\"Synthetic template #"};

struct Measured
{
    meta::MetaLogDocument doc;
    std::size_t emitted_bytes{0};
    std::size_t stats_only_bytes{0};
    std::string stats_only_json;
};

// post: template i carries floor(kHarmonicNumerator / (i + 1)) lines, template 0 also the
// remainder, so the window holds exactly kLines lines over kTemplates distinct templates.
[[nodiscard]] std::vector<std::uint64_t> harmonic_counts()
{
    std::vector<std::uint64_t> counts(kTemplates);
    std::uint64_t sum{0};
    for (std::size_t i{0}; i < kTemplates; ++i)
    {
        counts[i] = kHarmonicNumerator / (i + 1);
        sum += counts[i];
    }
    counts[0] += kLines - sum;
    return counts;
}

[[nodiscard]] Measured measure()
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.top_k_size = kTopK,
                                                   .reservoir_size = 0,
                                                   .top_ngrams_size = 0,
                                                   .emit_stability = false,
                                                   .top_branching_size = 0,
                                                   .dominant_path_max_steps = 0}};
    std::vector<std::string> templates;
    templates.reserve(kTemplates);
    for (std::size_t i{0}; i < kTemplates; ++i)
        templates.push_back("Synthetic template #" + std::to_string(i) +
                            " value=<*> latency_ms=<*> code=<*>");

    engine.open_window(kEpoch);
    const auto counts{harmonic_counts()};
    for (std::size_t i{0}; i < kTemplates; ++i)
        for (std::uint64_t n{0}; n < counts[i]; ++n)
            engine.ingest_event(make_event(templates[i]));

    Measured out;
    out.doc = engine.close_window(kEpoch + std::chrono::seconds{60});
    out.emitted_bytes = written_or_fail(meta::to_json(out.doc, engine.registry())).size();
    out.doc.has_cube = false;
    out.doc.acquisition.reset();
    out.stats_only_json = written_or_fail(meta::to_json(out.doc, engine.registry()));
    out.stats_only_bytes = out.stats_only_json.size();
    return out;
}
} // namespace

TEST(StatsOnlyEnvelopeSize, TopK32InlineOverOneMillionLines)
{
    const Measured measured{measure()};
    const meta::MetaLogDocument& doc{measured.doc};

    // assert: the document is the one this arm measures before its size means anything.
    ASSERT_EQ(doc.window.lines_observed, kLines);
    ASSERT_EQ(doc.stats.top_k.size(), kTopK)
        << "top_k must be FULL at its cap, or the arm measures a smaller document";
    ASSERT_TRUE(doc.stats.reservoir.empty()) << measured.stats_only_json;
    ASSERT_FALSE(doc.behavior.has_value()) << measured.stats_only_json;
    ASSERT_FALSE(doc.stability.has_value()) << measured.stats_only_json;
    std::size_t inline_templates{0};
    for (auto pos{measured.stats_only_json.find(kInlineTemplateKey)}; pos != std::string::npos;
         pos = measured.stats_only_json.find(kInlineTemplateKey, pos + 1))
        ++inline_templates;
    ASSERT_EQ(inline_templates, kTopK) << "every top_k entry must carry its template string inline";

    EXPECT_EQ(measured.stats_only_bytes, kStatsOnlyBytes)
        << "the stats-only envelope moved: " << measured.stats_only_bytes
        << " bytes against the pinned " << kStatsOnlyBytes
        << "; the document as emitted (cube and acquisition kept) is " << measured.emitted_bytes
        << " bytes.\n"
        << measured.stats_only_json;
}
