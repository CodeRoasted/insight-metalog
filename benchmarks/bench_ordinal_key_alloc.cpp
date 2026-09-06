// invariant: every arm reads 0 allocations per event on both toolchains; an arm reading 1 again is
// the accumulator's key construction coming back.
// invariant: every arm carries exactly ONE ordinal observation per event, so the key LENGTH is the
// only variable across arms.
// note: the 23-char arm proves the instrument sees an allocation on the dev leg too.
// refs: ADR-3.D4, ADR-9.D2
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

// invariant: the observations outlive the events that span them -- built first, never resized
// afterwards.
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
                // invariant: a fixed spread over 40 octaves of the ladder, with no RNG.
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
    // invariant: the gate the ordinal block sits behind; params stay empty, so the ordinal block is
    // the only variable this arm moves.
    config.max_param_histograms = 1;

    const Fixture fixture{make_fixture(keys)};

    // invariant: a fixed epoch, never the wall clock, so the window boundary is an input.
    const std::chrono::system_clock::time_point t0{std::chrono::seconds{1'700'000'000}};
    std::int64_t total_events{0};
    std::uint64_t loop_allocs{0};

    for (auto _ : state)
    {
        state.PauseTiming();
        meta::MetaLogEngine engine{config};
        engine.open_window(t0);
        // invariant: the warm lap is untimed and uncounted, so the timed lap sees steady state --
        // hits only, and any allocation counted is the lookup key, not table growth.
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
