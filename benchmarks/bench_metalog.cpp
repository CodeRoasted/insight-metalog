// post: reports the produced JSON envelope's size for a synthesised window whose template
// distribution is Zipf-ish, so the byte cost can be diffed across changes.
// note: the spec's 4 KB-per-million target is stats-only, so this arm is not measured on it.
// refs: F-SRC-metalog-spec:SPEC.md
#include <benchmark/benchmark.h>

import insight.metalog.bench;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// invariant: owns the synthetic template strings, so every CanonicalEvent string_view stays valid
// for the whole benchmark loop.
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

// post: opens a window, ingests n_events, closes it, and writes the envelope size and the line
// count into the out-params.
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
    const std::string serialized{to_json(doc, engine.registry())};
    out_bytes = serialized.size();
    out_lines = doc.window.lines_observed;
    out_unique = doc.stats.unique_templates;
}

// invariant: range(0) is the event count for the window and range(1) the top_k_size.
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

    // note: the absolute bytes ride the label because the counter machinery rescales them.
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

// post: the marginal ingest cost of per-param value histograms for a three-param HTTP shape, with
// range(0) the max_param_histograms setting.
// invariant: the corpus is 1 000 events per window with a fixed method and path and a one-in-five
// status split.
void BM_MetaLogIngest_FieldHistograms(benchmark::State& state)
{
    const std::size_t max_hist{static_cast<std::size_t>(state.range(0))};

    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;
    config.max_param_histograms = max_hist;

    // invariant: the fixtures own their param strings and are built before the loop, so no
    // allocation of theirs lands inside the timed region.
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
        std::uniform_int_distribution<int> coin{0, 4};
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
BENCHMARK(BM_MetaLogIngest_FieldHistograms)->Arg(0)->Arg(1)->Arg(3)->Unit(benchmark::kMicrosecond);

// post: the always-on cost of the per-template component marginal, which every event pays.
// note: read it against the field-histogram arm to size it beside an enum-keyed increment.
void BM_MetaLogIngest_Where(benchmark::State& state)
{
    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;

    constexpr std::size_t kEvents{1'000};
    // invariant: low-cardinality functional-source components, never host names.
    static constexpr std::array<std::string_view, 4> kComponents{"src/auth", "src/db", "src/api",
                                                                 "src/core"};
    static constexpr std::array<std::string_view, 4> kTemplates{
        "login ok", "pool timeout", "request served <*>", "cache warmed"};

    std::vector<tok::CanonicalEvent> events;
    events.reserve(kEvents);
    {
        std::mt19937 rng{0x7E57C0DE};
        std::uniform_int_distribution<std::size_t> pick{0, kComponents.size() - 1};
        for (std::size_t i{0}; i < kEvents; ++i)
        {
            tok::CanonicalEvent ev;
            const std::size_t idx{pick(rng)};
            ev.template_str = kTemplates[idx];
            // invariant: a static-storage view, so it stays valid for the whole run.
            ev.component = kComponents[idx];
            ev.level = insight::LogLevel::Info;
            events.push_back(ev);
        }
    }

    const auto t0{std::chrono::system_clock::now()};
    std::int64_t total_events{0};

    for (auto _ : state)
    {
        meta::MetaLogEngine engine{config};
        engine.open_window(t0);
        for (const auto& ev : events)
            engine.ingest_event(ev);
        auto doc{engine.close_window(t0 + std::chrono::seconds(60))};
        benchmark::DoNotOptimize(doc.stats.top_k.size());
        total_events += static_cast<std::int64_t>(kEvents);
    }

    state.SetItemsProcessed(total_events);
    state.counters["ns_per_event"] = benchmark::Counter(
        static_cast<double>(total_events),
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert, benchmark::Counter::kIs1000);
}
BENCHMARK(BM_MetaLogIngest_Where)->Unit(benchmark::kMicrosecond);

} // namespace
