// invariant: SPEC.md § 11.5's headline, at most 4 KB per MetaLog of at least 1 M lines, governs a
// stats-only document (no reservoir, behavior or cube) at top_k_size 32 inline; this arm builds it.
// invariant: the engine always emits the cube and the acquisition block, so the in-scope document
// is the closed window with those two cleared; every other block is off by configuration.
// invariant: the stream is built for the headline's most favourable case, so a miss here is a miss
// in the headline's own scope: no parameters (no param_histograms), one level, top_k full at 32.
// invariant: the counts are an integer harmonic series and the epoch is fixed, so the bytes are one
// number on every run and on both toolchains.
// refs: F-SRC-metalog-spec:SPEC.md
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

constexpr std::uint64_t kLines{1'000'000};
constexpr std::size_t kTemplates{64};
constexpr std::size_t kTopK{32};
constexpr std::uint64_t kHarmonicNumerator{210'000};
constexpr std::size_t kSpecHeadlineBytes{4096};
constexpr std::chrono::system_clock::time_point kEpoch{std::chrono::seconds{1'700'000'000}};

// invariant: a CHARACTERIZATION pin, measured by this arm and never guessed; a move is a change to
// the producer's bytes, re-measured and never refitted.
// invariant: the arm pins what the producer MEASURES in the headline's scope and never asserts the
// headline itself.
constexpr std::size_t kInScopeBytes{6'113};
constexpr std::string_view kInlineTemplateKey{"\"template\":\"Synthetic template #"};

struct Measured
{
    meta::MetaLogDocument doc;
    std::size_t emitted_bytes{0};
    std::size_t in_scope_bytes{0};
    std::string in_scope_json;
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
    out.emitted_bytes = meta::to_json(out.doc, engine.registry()).size();
    out.doc.has_cube = false;
    out.doc.acquisition.reset();
    out.in_scope_json = meta::to_json(out.doc, engine.registry());
    out.in_scope_bytes = out.in_scope_json.size();
    return out;
}
} // namespace

TEST(SpecEnvelopeHeadline, StatsOnlyDocumentAtTopK32InlineOverOneMillionLines)
{
    const Measured measured{measure()};
    const meta::MetaLogDocument& doc{measured.doc};

    // assert: the document is in the headline's scope before its size means anything.
    ASSERT_EQ(doc.window.lines_observed, kLines);
    ASSERT_EQ(doc.stats.top_k.size(), kTopK)
        << "top_k must be FULL at the headline's cap, or the arm measures a smaller document";
    ASSERT_TRUE(doc.stats.reservoir.empty()) << measured.in_scope_json;
    ASSERT_FALSE(doc.behavior.has_value()) << measured.in_scope_json;
    ASSERT_FALSE(doc.stability.has_value()) << measured.in_scope_json;
    std::size_t inline_templates{0};
    for (auto pos{measured.in_scope_json.find(kInlineTemplateKey)}; pos != std::string::npos;
         pos = measured.in_scope_json.find(kInlineTemplateKey, pos + 1))
        ++inline_templates;
    ASSERT_EQ(inline_templates, kTopK)
        << "every top_k entry must carry its template string inline, the headline's inline mode";

    EXPECT_EQ(measured.in_scope_bytes, kInScopeBytes)
        << "the in-scope envelope moved: " << measured.in_scope_bytes
        << " bytes against the pinned " << kInScopeBytes << "; SPEC.md § 11.5's headline is "
        << kSpecHeadlineBytes
        << " bytes, and the document as emitted (cube and acquisition kept) is "
        << measured.emitted_bytes << " bytes.\n"
        << measured.in_scope_json;
}
