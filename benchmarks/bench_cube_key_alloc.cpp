// NOLINTBEGIN(readability-magic-numbers) — a benchmark: the component lengths ARE the data.
//
// bench_cube_key_alloc.cpp — the measure-first arm for the cube-key heap row
// (bugs.md 2026-08-02): MetaLogEngine::ingest_event builds two map keys as
// `std::string` per event (cube_base_'s tuple key, unconditional; component_counts',
// when the component is non-empty), and ADR-9.D2 claims the hot path is arena/bounded.
//
// WHAT THIS MEASURES, and why the existing WHERE bench cannot: BM_MetaLogIngest_Where
// drives 8-char components — inside the SSO band on BOTH stdlibs — so it is
// structurally blind to the allocation the row is about. The SSO boundary is the trap:
// libstdc++ inlines ≤ 15 chars, libc++ ≤ 22, so a 16-22-char component allocates on the
// SHIP leg and not on the DEV leg. The arms below straddle the band on purpose:
//
//   empty  ""                                  0 B  no component_counts entry at all
//   short  "src/auth"                          8 B  SSO on both
//   mid    "/aws/lambda/myFunc"               18 B  allocates on libstdc++ ONLY
//   long   "src/components/Button.spec.tsx"   30 B  allocates on both
//
// mid and long are the row's own attested shapes (cloud log-group components; canon's
// recognize_location() test-file fill). STEADY STATE is the load-bearing condition: the
// event stream cycles 4 keys, so after the first lap every map access is a HIT — any
// allocation seen per event is the LOOKUP key being materialised, not table growth.
//
// Two readouts per arm (heap_probe.hpp is the instrument — a counting passthrough on the
// global heap, armed only inside the ingest loop):
//   ns_per_event     — the real ingest cost (the share question)
//   allocs_per_event — global operator new count / events, counted ONLY inside the
//                      ingest loop
// Attribution is by SUBTRACTION against the `empty` arm, everything else identical.

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
        ev.component = component; // static-storage view: stays valid across the run
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
        // WARM LAP, untimed and uncounted: every key enters its map here, so the timed
        // laps below observe pure steady state — hits only, no table growth.
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

// NOLINTEND(readability-magic-numbers)
