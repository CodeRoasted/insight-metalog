// latency_shift_scenario.hpp — the shared synthetic latency-drift window PAIR: the one scenario in
// this repo whose DIFF carries a `cube_diff` with the diff-only differential axis.
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same drift:
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite non-hollowness guard.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc×clang × -O{0,3} × -ffp-contract{off,fast}), --latency-shift.
//
// WHY A PAIR AND NOT A WINDOW. Every other scenario here emits ONE window and the oracles judge a
// MetaLogDocument. The `latency_shift` axis (cube_differential_axes.md §4) is EMERGENT-AT-DIFF: it
// has no stored-cube domain and appears only on a `cube_diff`, so no single-window scenario — and
// therefore no section of the determinism digest as it stood — could ever produce one. A
// MetaLogDiff is a second artifact species this producer serializes (`to_json(const MetaLogDiff&)`)
// and that insight-eidos re-publishes verbatim inside every Sift change report
// (sift/src/report/change_report_serialize.cpp, `raw[].diff`); until this scenario existed, that
// species had never been replayed across a leg, and the schema that governs it
// (metalog_diff.v0.schema.json) had never been opened for our own output.
//
// WHY THESE NUMBERS. `payments` moves 100 ms → 100 s: a 10-octave move on the frozen 48-bin
// floor(log2 ns) ladder, comfortably inside the W1 HIGH bucket, so the emitted band does not sit on
// a threshold that a ladder retune would silently move across. 40 events per component clears
// `ComponentOrdinal::kShiftSampleFloor` (32, studies/003, frozen) on BOTH sides — below it the axis
// is correctly projected away and this scenario would emit a plain 3-D border while still looking
// like it worked. `auth` is a STABLE second component carrying no latency at all: with a single
// component the closure stars the WHERE dimension away, and the shifted cell would be the
// aggregate rather than a pinned (where=payments, latency_shift=…) coord.
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports. Both includers are plain TUs (not modules), so
// this is ordinary textual inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_LATENCY_SHIFT_SCENARIO_HPP
#define INSIGHT_METALOG_LATENCY_SHIFT_SCENARIO_HPP

namespace insight::metalog::latency_shift
{

// The DurationLog2Ns schedule is nanoseconds; the scenario states its latencies in ms.
inline constexpr std::int64_t kMsToNs{1'000'000};
inline constexpr std::int64_t kPreviousLatencyMs{100};    // 1e8 ns → ladder bin 26
inline constexpr std::int64_t kCurrentLatencyMs{100'000}; // 1e11 ns → ladder bin 36
inline constexpr std::size_t kEventsPerComponent{40};     // ≥ kShiftSampleFloor (32) on both sides

// `max_param_histograms > 0` is the batch gate that turns the ordinal (DurationLog2Ns) histograms
// on — without them `aggregate_duration_by_component` finds nothing and the axis never appears.
// Stability off and a small reservoir keep the emitted pair about the cube and its differential
// axis rather than about regimes the other scenarios already own.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.reservoir_size = 8;
    cfg.reservoir_per_kind_cap = 4;
    cfg.emit_stability = false;
    cfg.max_param_histograms = 2;
}

// Drive ONE window: `payments` at `latency_ms`, `auth` stable and latency-free. Caller does
// open_window / close_window around this, and calls it TWICE on the SAME engine (a shared registry
// and a shared processing contract, so diff()'s §2.4 comparability gate passes by construction
// rather than by a hand-copied identifier). Deterministic given configure() above.
inline void emit_window(insight::metalog::MetaLogEngine& engine, std::int64_t latency_ms)
{
    // One observation, re-valued per event. `CanonicalEvent::ordinals` is a non-owning span and
    // ingest_event copies out of it, so the storage only has to outlive the call.
    std::vector<insight::OrdinalObservation> observation(
        1, insight::OrdinalObservation{.field_name = "latency_ms",
                                       .schedule = insight::OrdinalSchedule::DurationLog2Ns,
                                       .value = latency_ms * kMsToNs});
    for (std::size_t i = 0; i < kEventsPerComponent; ++i)
    {
        insight::tokenization::CanonicalEvent event;
        event.template_str = "charge card <*>";
        event.level = insight::LogLevel::Info;
        event.component = "payments";
        event.ordinals = observation;
        engine.ingest_event(event);
    }
    for (std::size_t i = 0; i < kEventsPerComponent; ++i)
    {
        insight::tokenization::CanonicalEvent event;
        event.template_str = "auth ok";
        event.level = insight::LogLevel::Info;
        event.component = "auth";
        engine.ingest_event(event);
    }
}

} // namespace insight::metalog::latency_shift

#endif // INSIGHT_METALOG_LATENCY_SHIFT_SCENARIO_HPP
