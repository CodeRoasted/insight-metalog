// NOLINTBEGIN
// MetaLog v0.5.0 compression benchmark.
//
// This is the byte-budget anchor for Phase 3. It synthesises a window
// of CanonicalEvents with a Zipf-ish template distribution, runs them
// through MetaLogEngine, and reports the size of the produced JSON
// envelope so we can diff it across PRs.
//
// Two reported counters:
//   * BytesPerWindow    — size of the JSON envelope, in bytes.
//   * BytesPerMillion   — extrapolated bytes per million lines.
//
// The "≤ 4 KB per million lines" architectural target lives in
// technical_docs/overview/architecture.md §6 and is aspirational; this bench
// is what we measure against when we go after it.

#include <benchmark/benchmark.h>

import std;
import insight.metalog;
import insight.canon;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// Storage that owns the synthetic template strings so the
// `string_view`s on `CanonicalEvent` stay valid for the duration of
// the benchmark loop.
struct SyntheticCorpus
{
    std::vector<std::string> templates;
};

SyntheticCorpus make_corpus(std::size_t n_templates)
{
    SyntheticCorpus c;
    c.templates.reserve(n_templates);
    for (std::size_t i = 0; i < n_templates; ++i)
    {
        c.templates.push_back("Synthetic template #" + std::to_string(i) +
                              " value=<*> latency_ms=<*> code=<*>");
    }
    return c;
}

// One run of the engine: open window, ingest n_events, close, write
// the envelope size + lines into out-params.
void run_once(const SyntheticCorpus& corpus, std::size_t n_events,
              const meta::MetaLogConfig& config, std::uint32_t seed, std::size_t& out_bytes,
              std::uint64_t& out_lines, std::size_t& out_unique)
{
    meta::MetaLogEngine engine{config};
    const auto t0{std::chrono::system_clock::now()};
    engine.open_window(t0);

    std::mt19937 rng{seed};
    const std::size_t n_templates = corpus.templates.size();

    for (std::size_t i = 0; i < n_events; ++i)
    {
        const double u = std::uniform_real_distribution<double>{0.0, 1.0}(rng);
        const std::size_t t_idx = static_cast<std::size_t>(u * u * n_templates) % n_templates;

        tok::CanonicalEvent ev;
        ev.template_str = corpus.templates[t_idx];
        ev.level = insight::LogLevel::Info;
        engine.ingest_event(ev);
    }

    auto doc{engine.close_window(t0 + std::chrono::seconds(60))};
    const std::string serialized{to_json(doc)};
    out_bytes = serialized.size();
    out_lines = doc.window.lines_observed;
    out_unique = doc.stats.unique_templates;
}

// state.range(0) = number of events ingested in the window.
// state.range(1) = top_k_size (controls the compression budget).
void BM_MetaLogCompress(benchmark::State& state)
{
    const std::size_t n_events = static_cast<std::size_t>(state.range(0));
    const std::size_t top_k = static_cast<std::size_t>(state.range(1));

    constexpr std::size_t kTemplates = 256;
    auto corpus{make_corpus(kTemplates)};

    meta::MetaLogConfig config;
    config.top_k_size = top_k;
    config.top_ngrams_size = 32;
    config.max_ngram_keys = 4096;

    std::size_t last_bytes = 0;
    std::uint64_t last_lines = 0;
    std::size_t last_unique = 0;
    std::uint32_t seed = 0x5A1F00D;

    for (auto _ : state)
    {
        run_once(corpus, n_events, config, seed++, last_bytes, last_lines, last_unique);
        benchmark::DoNotOptimize(last_bytes);
    }

    // Counters get aggregated/scaled by Google Benchmark in ways
    // that don't match what we want for raw envelope size. We embed
    // the absolute bytes (window + extrapolated /M lines) in the
    // label, which the runner prints verbatim.
    const double per_million = last_lines > 0 ? static_cast<double>(last_bytes) * 1'000'000.0 /
                                                    static_cast<double>(last_lines)
                                              : 0.0;

    char label[256];
    std::snprintf(label, sizeof(label), "json_bytes=%zu unique=%zu per_million=%.0f top_k=%zu",
                  last_bytes, last_unique, per_million, top_k);
    state.SetLabel(label);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n_events));
}

BENCHMARK(BM_MetaLogCompress)
    ->ArgsProduct({{1'000, 10'000, 100'000}, {16, 32, 64}})
    ->Unit(benchmark::kMillisecond);

// ── Field histogram ingest-cost benchmark ─────────────────────────────────────
//
// Measures the marginal cost of per-param value histogram accumulation inside
// ingest_event() for a realistic HTTP scenario (3 params: method, path, status).
//
// state.range(0) = max_param_histograms
//   0 → disabled (baseline, one branch per event, zero extra work)
//   1 → track params[0] only (path distribution)
//   3 → track all 3 params (full field observability)
//
// Corpus: 1 000 events per window iteration, fixed method+path, Bernoulli(0.2)
// status — matches the detection test scenario.
//
// Reported:
//   items/s      — ingest_event calls per second
//   ns_per_event — average nanoseconds per call
//
// Use this to check whether enabling histograms crosses any budget threshold.
// On typical workloads (< 64 distinct values per slot) the map lookups are all
// cache-warm; expect single-digit ns overhead per param slot.

void BM_MetaLogIngest_FieldHistograms(benchmark::State& state)
{
    const std::size_t max_hist{static_cast<std::size_t>(state.range(0))};

    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;
    config.max_param_histograms = max_hist;

    // Pre-build event fixtures with owned param strings so the benchmark
    // loop is hot and allocation is excluded from timing.
    constexpr std::size_t kEvents{1'000};

    struct Fixture
    {
        std::vector<std::string> owned;
        std::vector<std::string_view> views;
        tok::CanonicalEvent event;
    };

    std::vector<Fixture> fixtures;
    fixtures.reserve(kEvents);
    {
        std::mt19937 rng{0x1A2B3C4D};
        std::uniform_int_distribution<int> coin{0, 4}; // ~20 % "500"
        for (std::size_t i{0}; i < kEvents; ++i)
        {
            Fixture f;
            f.owned = {"GET", "/api/users", coin(rng) == 0 ? "500" : "200"};
            for (const auto& s : f.owned)
                f.views.push_back(s);
            f.event.template_str = "GET <*> -> <*>";
            f.event.level = insight::LogLevel::Info;
            f.event.params = f.views;
            fixtures.push_back(std::move(f));
        }
    }

    const auto t0{std::chrono::system_clock::now()};
    std::int64_t total_events{0};

    for (auto _ : state)
    {
        meta::MetaLogEngine engine{config};
        engine.open_window(t0);
        for (const auto& f : fixtures)
            engine.ingest_event(f.event);
        auto doc{engine.close_window(t0 + std::chrono::seconds(60))};
        benchmark::DoNotOptimize(doc.stats.top_k.size());
        total_events += static_cast<std::int64_t>(kEvents);
    }

    state.SetItemsProcessed(total_events);
    state.counters["ns_per_event"] = benchmark::Counter(
        static_cast<double>(total_events),
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert, benchmark::Counter::kIs1000);
}
BENCHMARK(BM_MetaLogIngest_FieldHistograms)
    ->Arg(0) // disabled (baseline)
    ->Arg(1) // track 1 param slot
    ->Arg(3) // track all 3 param slots
    ->Unit(benchmark::kMicrosecond);

} // namespace

// NOLINTEND
