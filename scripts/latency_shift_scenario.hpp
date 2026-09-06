// invariant: the one scenario here whose DIFF carries a cube_diff with the diff-only differential
// axis, which is emergent-at-diff and has no stored-cube domain.
// invariant: a PAIR rather than a window, because no single-window scenario can produce that axis.
// note: a MetaLogDiff is the second artifact species this producer serialises.
#ifndef INSIGHT_METALOG_LATENCY_SHIFT_SCENARIO_HPP
#define INSIGHT_METALOG_LATENCY_SHIFT_SCENARIO_HPP

namespace insight::metalog::latency_shift
{

// invariant: the schedule is nanoseconds; this scenario states its latencies in ms.
inline constexpr std::int64_t kMsToNs{1'000'000};
inline constexpr std::int64_t kPreviousLatencyMs{100};
inline constexpr std::int64_t kCurrentLatencyMs{100'000};
inline constexpr std::size_t kEventsPerComponent{40};

// invariant: max_param_histograms > 0 is the gate that turns the ordinal histograms on -- without
// them the aggregation finds nothing and the axis never appears.
// note: stability off and a small reservoir keep the pair about the cube.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.reservoir_size = 8;
    cfg.reservoir_per_kind_cap = 4;
    cfg.emit_stability = false;
    cfg.max_param_histograms = 2;
}

// pre: called TWICE on the SAME engine, so the shared registry and processing contract make the
// comparability gate pass by construction rather than by a hand-copied identifier.
// post: payments carries the latency and auth is a stable second component.
inline void emit_window(insight::metalog::MetaLogEngine& engine, std::int64_t latency_ms)
{
    // invariant: CanonicalEvent::ordinals is a non-owning span and ingest_event copies out of it,
    // so the storage only has to outlive the call.
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

#endif
