// NOLINTBEGIN(readability-magic-numbers) — a benchmark: the key lengths ARE the data.
//
// bench_ordinal_key_alloc.cpp — the measure-first arm for the W1 ordinal accumulator's key
// (ROADMAP N102), now its standing regression guard at 0. MetaLogEngine::ingest_event used to do
//     bucket.ordinal_accumulators.try_emplace(std::string{observation.field_name})
// on a NON-transparent map, so every declared ordinal observation of every event constructed a
// std::string key — on a repeat hit too, where the key was only looked up and then destroyed.
// Three of the fifteen kOrdinalFieldCatalog keys are exactly 16 chars (span_duration_ns,
// response_time_ms, duration_seconds): over libstdc++'s 15-char SSO band and under libc++'s 22,
// so the construction heap-allocated on the gcc/libstdc++ SHIP leg and never on the clang/libc++
// dev leg (MEM:toolchain-clang21-dev-gcc16-ship) — measured here at 1 allocation per observation
// per event and +7.9 ns/event over the 15-char control (insight-metalog 71a74af). The map now
// carries the transparent find-then-copy form param_value_counts took in b5883f6 (the key is
// copied on first sight only), so every arm below reads 0 allocations on both legs; an arm
// reading 1 again is the key construction coming back.
//
// ARMS. Every arm carries ONE observation per event — the OTEL span shape: a span yields exactly
// one span_duration_ns (canon json.cpp, parse_otel_span), a structured HTTP record one
// response_time_ms or duration_seconds — so the key LENGTH is the only variable across arms:
//   none      no ordinal at all          the block is never entered — the control every other
//                                        arm is subtracted against
//   key15     elapsed_seconds            a real catalog key, 15 chars: SSO on BOTH stdlibs — the
//                                        negative control (0 allocations on both legs, or the
//                                        instrument is reading something other than the key)
//   key16     span_duration_ns           a real catalog key, 16 chars: allocates on libstdc++ ONLY
//   key16mix  the three 16-char keys cycling per event (a trace stream interleaved with two
//                                        structured HTTP shapes): three accumulators, one length
//                                        — the count is per OBSERVATION, whichever key it is
//   key23     http_server_duration_ms    23 chars, NOT a catalog key (the engine keys on the bytes
//                                        it is handed and validates nothing): over BOTH SSO bands,
//                                        so the instrument is proven to see an allocation on the
//                                        dev leg too — a leg reading 0 on key16 AND 0 here would
//                                        be a blind instrument, not a clean path
// STEADY STATE is the load-bearing condition, exactly as in bench_cube_key_alloc: an untimed,
// uncounted warm lap enters every accumulator, so the counted lap is hits only and an allocation
// seen per event is the LOOKUP key being materialised, never table growth. The workload is
// deterministic — the ordinal values follow a fixed integer pattern, the window opens at a fixed
// epoch, and no RNG and no wall clock reach the loop.
//
// Two readouts per arm (heap_probe.hpp is the instrument):
//   ns_per_event     — the ingest cost with the ordinal path enabled (max_param_histograms > 0,
//                      the gate the block shares with the param histograms; params stay empty so
//                      that loop never runs and the ordinal block is the only variable)
//   allocs_per_event — global operator new count / events, counted only inside the timed loop

#include "heap_probe.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

import insight.metalog;
import insight.canon;

namespace meta = insight::metalog;
namespace tok = insight::tokenization;
using insight::metalog::bench::AllocCountScope;

namespace
{

constexpr std::size_t kEvents{1'000};
constexpr std::array<std::string_view, 4> kTemplates{"span http.server", "span db.query",
                                                     "span cache.get", "span queue.publish"};

// The key lengths are the data; the catalog membership of each real key is PINNED, so the arm
// names stay true if the catalog is ever edited (a renamed key would silently turn a "real
// catalog key" arm into a synthetic one).
constexpr std::string_view kKey15{"elapsed_seconds"};
constexpr std::string_view kKey16{"span_duration_ns"};
constexpr std::array<std::string_view, 3> kKey16Mix{"span_duration_ns", "response_time_ms",
                                                    "duration_seconds"};
constexpr std::string_view kKey23{"http_server_duration_ms"};
static_assert(kKey15.size() == 15, "the 15-char arm must sit exactly AT libstdc++'s SSO limit");
static_assert(kKey16.size() == 16, "the 16-char arm must sit one past libstdc++'s SSO limit");
static_assert(kKey16Mix[0].size() == 16 && kKey16Mix[1].size() == 16 && kKey16Mix[2].size() == 16,
              "every mixed-arm key must be exactly 16 chars — one length, three accumulators");
static_assert(kKey23.size() == 23, "the 23-char arm must sit one past libc++'s SSO limit");
static_assert(insight::match_ordinal_field(kKey15) != nullptr, "elapsed_seconds is a catalog key");
static_assert(insight::match_ordinal_field(kKey16) != nullptr, "span_duration_ns is a catalog key");
static_assert(insight::match_ordinal_field(kKey16Mix[1]) != nullptr &&
                  insight::match_ordinal_field(kKey16Mix[2]) != nullptr,
              "response_time_ms and duration_seconds are catalog keys");
static_assert(insight::match_ordinal_field(kKey23) == nullptr,
              "the 23-char key is deliberately synthetic — the catalog has no key past 16 chars");

// The observations must outlive the events that span them: built first, never resized after.
struct Fixture
{
    std::vector<insight::OrdinalObservation> observations;
    std::vector<tok::CanonicalEvent> events;
};

Fixture make_fixture(std::span<const std::string_view> keys)
{
    Fixture fixture;
    fixture.observations.reserve(kEvents);
    fixture.events.reserve(kEvents);
    if (!keys.empty())
        for (std::size_t i{0}; i < kEvents; ++i)
            fixture.observations.push_back(insight::OrdinalObservation{
                .field_name = keys[i % keys.size()],
                .schedule = insight::OrdinalSchedule::DurationLog2Ns,
                // A fixed spread over 40 octaves of the ladder — deterministic, no RNG.
                .value = std::int64_t{1} << (i % 40)});
    for (std::size_t i{0}; i < kEvents; ++i)
    {
        tok::CanonicalEvent ev;
        ev.template_str = kTemplates[i % kTemplates.size()];
        ev.level = insight::LogLevel::Info;
        if (!keys.empty())
            ev.ordinals = std::span<const insight::OrdinalObservation>{&fixture.observations[i], 1};
        fixture.events.push_back(ev);
    }
    return fixture;
}

void bench_ordinal_key_alloc(benchmark::State& state, std::span<const std::string_view> keys)
{
    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;
    config.max_param_histograms = 1; // the gate the ordinal block sits behind (engine.cpp)

    const Fixture fixture{make_fixture(keys)};

    // A fixed epoch, not the wall clock: the window boundary is part of the workload's inputs.
    const std::chrono::system_clock::time_point t0{std::chrono::seconds{1'700'000'000}};
    std::int64_t total_events{0};
    std::uint64_t loop_allocs{0};

    for (auto _ : state)
    {
        state.PauseTiming();
        meta::MetaLogEngine engine{config};
        engine.open_window(t0);
        // WARM LAP, untimed and uncounted: every accumulator is created here, so the timed lap
        // below observes pure steady state — hits only, no table growth.
        for (const auto& ev : fixture.events)
            engine.ingest_event(ev);
        state.ResumeTiming();

        {
            const AllocCountScope counting;
            for (const auto& ev : fixture.events)
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

void BM_OrdinalKeyAlloc_None(benchmark::State& state)
{
    bench_ordinal_key_alloc(state, {});
}
void BM_OrdinalKeyAlloc_Key15Sso(benchmark::State& state)
{
    bench_ordinal_key_alloc(state, std::span<const std::string_view>{&kKey15, 1});
}
void BM_OrdinalKeyAlloc_Key16ShipLegOnly(benchmark::State& state)
{
    bench_ordinal_key_alloc(state, std::span<const std::string_view>{&kKey16, 1});
}
void BM_OrdinalKeyAlloc_Key16TraceMix(benchmark::State& state)
{
    bench_ordinal_key_alloc(state, kKey16Mix);
}
void BM_OrdinalKeyAlloc_Key23OverBothSso(benchmark::State& state)
{
    bench_ordinal_key_alloc(state, std::span<const std::string_view>{&kKey23, 1});
}

} // namespace

BENCHMARK(BM_OrdinalKeyAlloc_None)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_OrdinalKeyAlloc_Key15Sso)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_OrdinalKeyAlloc_Key16ShipLegOnly)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_OrdinalKeyAlloc_Key16TraceMix)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_OrdinalKeyAlloc_Key23OverBothSso)->Unit(benchmark::kMicrosecond);

// NOLINTEND(readability-magic-numbers)
