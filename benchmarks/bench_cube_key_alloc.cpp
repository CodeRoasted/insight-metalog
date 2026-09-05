// invariant: the arms straddle the two stdlibs' small-string bands on purpose -- libstdc++ inlines
// up to 15 chars and libc++ up to 22, so a 16-to-22-char component allocates on
// invariant: the ship leg only. Empty, 8, 18 and 30 chars are the four arm lengths.
// invariant: allocation is attributed by SUBTRACTION against the empty arm, every other input being
// identical.
// refs: ADR-9.D2
#include "heap_probe.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

import insight.metalog;
import insight.canon;

namespace meta = insight::metalog;
namespace tok = insight::tokenization;
using insight::metalog::bench::AllocCountScope;

namespace
{

void bench_cube_key_alloc(benchmark::State& state, std::string_view component)
{
    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;

    constexpr std::size_t kEvents{1'000};
    static constexpr std::array<std::string_view, 4> kTemplates{
        "login ok", "pool timeout", "request served <*>", "cache warmed"};

    std::vector<tok::CanonicalEvent> events;
    events.reserve(kEvents);
    for (std::size_t i{0}; i < kEvents; ++i)
    {
        tok::CanonicalEvent ev;
        ev.template_str = kTemplates[i % kTemplates.size()];
        // invariant: a static-storage view, so it stays valid for the whole run.
        ev.component = component;
        ev.level = insight::LogLevel::Info;
        events.push_back(ev);
    }

    const auto t0{std::chrono::system_clock::now()};
    std::int64_t total_events{0};
    std::uint64_t loop_allocs{0};

    for (auto _ : state)
    {
        state.PauseTiming();
        meta::MetaLogEngine engine{config};
        engine.open_window(t0);
        // invariant: the warm lap is untimed and uncounted, so the timed laps see steady state --
        // every map access is a hit and any allocation counted is the lookup key, not table growth.
        for (const auto& ev : events)
            engine.ingest_event(ev);
        state.ResumeTiming();

        {
            const AllocCountScope counting;
            for (const auto& ev : events)
                engine.ingest_event(ev);
            loop_allocs += AllocCountScope::count();
        }

        state.PauseTiming();
        auto doc{engine.close_window(t0 + std::chrono::seconds(60))};
        benchmark::DoNotOptimize(doc.stats.top_k.size());
        state.ResumeTiming();

        total_events += static_cast<std::int64_t>(kEvents);
    }

    state.SetItemsProcessed(total_events);
    state.counters["ns_per_event"] =
        benchmark::Counter(static_cast<double>(total_events),
                           benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
    state.counters["allocs_per_event"] =
        benchmark::Counter(static_cast<double>(loop_allocs) /
                           static_cast<double>(total_events == 0 ? 1 : total_events));
}

void BM_CubeKeyAlloc_Empty(benchmark::State& state)
{
    bench_cube_key_alloc(state, "");
}
void BM_CubeKeyAlloc_ShortSSO(benchmark::State& state)
{
    bench_cube_key_alloc(state, "src/auth");
}
void BM_CubeKeyAlloc_MidBand(benchmark::State& state)
{
    bench_cube_key_alloc(state, "/aws/lambda/myFunc");
}
void BM_CubeKeyAlloc_LongOverSSO(benchmark::State& state)
{
    bench_cube_key_alloc(state, "src/components/Button.spec.tsx");
}

} // namespace

BENCHMARK(BM_CubeKeyAlloc_Empty)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_CubeKeyAlloc_ShortSSO)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_CubeKeyAlloc_MidBand)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_CubeKeyAlloc_LongOverSSO)->Unit(benchmark::kMicrosecond);
