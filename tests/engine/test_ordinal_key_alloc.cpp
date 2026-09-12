// refs: ADR-9.D2, ADR-3.D4
// invariant: the ENFORCED form of the zero-allocation guard `bench_ordinal_key_alloc.cpp` measures
// — steady-state ingest carrying ordinal observations allocates NOTHING per event, on both legs.
// invariant: the key lengths straddle both standard libraries' small-string limits (15 and 22), so
// an accumulator lookup that builds a std::string key allocates on at least one leg and reds here.
// invariant: the counter is the benchmark's own probe, compiled into this binary once, so the guard
// and the measurement read one instrument.
// invariant: determinism — a fixed fixture, a fixed epoch, no RNG and no float.
#include "heap_probe.hpp"

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
namespace tok = insight::tokenization;
using insight::metalog::bench::AllocCountScope;

constexpr std::size_t kEvents{1'000};
constexpr std::array<std::string_view, 4> kTemplates{"span http.server", "span db.query",
                                                     "span cache.get", "span queue.publish"};

constexpr std::string_view kKey15{"elapsed_seconds"};
constexpr std::string_view kKey16{"span_duration_ns"};
constexpr std::array<std::string_view, 3> kKey16Mix{"span_duration_ns", "response_time_ms",
                                                    "duration_seconds"};
constexpr std::string_view kKey23{"http_server_duration_ms"};
static_assert(kKey15.size() == 15 && kKey16.size() == 16 && kKey23.size() == 23,
              "the arms must sit at libstdc++'s SSO limit, one past it, and one past libc++'s");

struct Arm
{
    std::string_view name;
    std::span<const std::string_view> keys;
};

// invariant: the observations are reserved before any event spans one, so no view dangles.
struct Fixture
{
    std::vector<insight::OrdinalObservation> observations;
    std::vector<tok::CanonicalEvent> events;
};

[[nodiscard]] Fixture make_fixture(std::span<const std::string_view> keys)
{
    Fixture fixture;
    fixture.observations.reserve(kEvents);
    fixture.events.reserve(kEvents);
    if (!keys.empty())
        for (std::size_t index{0}; index < kEvents; ++index)
            fixture.observations.push_back(
                insight::OrdinalObservation{.field_name = keys[index % keys.size()],
                                            .schedule = insight::OrdinalSchedule::DurationLog2Ns,
                                            .value = std::int64_t{1} << (index % 40)});
    for (std::size_t index{0}; index < kEvents; ++index)
    {
        tok::CanonicalEvent event;
        event.template_str = kTemplates[index % kTemplates.size()];
        event.level = insight::LogLevel::Info;
        if (!keys.empty())
            event.ordinals =
                std::span<const insight::OrdinalObservation>{&fixture.observations[index], 1};
        fixture.events.push_back(event);
    }
    return fixture;
}

} // namespace

// invariant: the probe's POSITIVE control — a 23-byte string exceeds both small-string limits, so
// building one inside the scope must count, or every zero below is an unarmed probe.
// assert: the string ESCAPES into a static, because an allocation nothing observes may be elided
// by the optimiser, and measured on clang-21 a local one was — the control then read zero.
TEST(OrdinalKeyAllocation, TheProbeCountsAnAllocationItCanSee)
{
    static std::string escaped;
    std::uint64_t counted{0};
    {
        const AllocCountScope counting;
        escaped = std::string{kKey23};
        counted = AllocCountScope::count();
    }
    EXPECT_EQ(escaped, kKey23);
    EXPECT_GE(counted, 1U) << "a 23-byte std::string allocated nothing the probe could see — the "
                              "probe is not armed in this binary, so a zero below proves nothing";
}

TEST(OrdinalKeyAllocation, SteadyStateIngestAllocatesNothingPerEvent)
{
    const std::array<Arm, 5> arms{{{.name = "no ordinals", .keys = {}},
                                   {.name = "15-byte key", .keys = {&kKey15, 1}},
                                   {.name = "16-byte key", .keys = {&kKey16, 1}},
                                   {.name = "three 16-byte keys", .keys = kKey16Mix},
                                   {.name = "23-byte key", .keys = {&kKey23, 1}}}};

    meta::MetaLogConfig config;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;
    config.max_param_histograms = 1;
    const std::chrono::system_clock::time_point epoch{std::chrono::seconds{1'700'000'000}};

    for (const Arm& arm : arms)
    {
        const Fixture fixture{make_fixture(arm.keys)};
        meta::MetaLogEngine engine{config};
        engine.open_window(epoch);
        // invariant: the warm lap is uncounted, so the counted lap sees hits only — any allocation
        // counted is the per-event lookup, never first-sight table growth.
        for (const auto& event : fixture.events)
            engine.ingest_event(event);
        std::uint64_t counted{0};
        {
            const AllocCountScope counting;
            for (const auto& event : fixture.events)
                engine.ingest_event(event);
            counted = AllocCountScope::count();
        }
        EXPECT_EQ(counted, 0U) << arm.name << ": " << counted << " allocations over " << kEvents
                               << " steady-state events (" << static_cast<double>(counted) / kEvents
                               << " per event) — an ordinal accumulator lookup is building its key "
                                  "again";
        const auto doc{engine.close_window(epoch + std::chrono::seconds{60})};
        EXPECT_EQ(doc.window.lines_observed, 2 * kEvents)
            << arm.name << ": the laps did not ingest";
    }
}
