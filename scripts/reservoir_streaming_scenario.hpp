// invariant: the SECOND near-full reservoir arm, at the retention tuple the streaming surface
// ships, where the error-class RESERVE is live and the batch arm has no opinion.
// invariant: the window is re-dimensioned rather than replayed under a second config -- the batch
// window under this top_k pulls every ambiguous spoke above the cut and goes hollow.
// note: this file is a retention oracle, not a production mirror.
// refs: ADR-31.D8, ADR-20.D7
#ifndef INSIGHT_METALOG_RESERVOIR_STREAMING_SCENARIO_HPP
#define INSIGHT_METALOG_RESERVOIR_STREAMING_SCENARIO_HPP

namespace insight::metalog::streaming_nearfull
{

// invariant: the shipped streaming retention tuple, every member assigned rather than inherited, so
// an unstated member cannot put the measurement at half of production.
inline constexpr std::size_t kTopK{128};
inline constexpr std::size_t kReservoir{64};
inline constexpr std::size_t kPerKindCap{0};
inline constexpr std::size_t kErrorReserve{16};

// invariant: the population is spelled as constants because the in-suite guard asserts arithmetic
// over them, so a silent re-dimensioning reds rather than shifts the boundary.
inline constexpr int kFillers{122};
inline constexpr int kErrorTemplates{24};
inline constexpr int kSolidSpokes{40};
inline constexpr int kAmbiguousSpokes{24};

inline constexpr std::size_t kGeneralSlots{kReservoir - kErrorReserve};

inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.top_k_size = kTopK;
    cfg.reservoir_size = kReservoir;
    cfg.reservoir_per_kind_cap = kPerKindCap;
    cfg.reservoir_error_reserve = kErrorReserve;
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// post: one window whose reservoir fills to M with BOTH the error reserve and the equal-ratio edge
// tie-break deciding membership.
// invariant: every salient template is seen 2 or 3 times, so the rarity modulator is uniform and
// the ranking is the severity band alone.
// invariant: the window is over-subscribed at every tier -- the fillers plus hubs are exactly
// top_k, and the general pool is filled by one band alone.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    const auto emit =
        [&engine](const std::string& tmpl, insight::LogLevel lvl = insight::LogLevel::Info)
    {
        insight::tokenization::CanonicalEvent ev;
        ev.template_str = tmpl;
        ev.level = lvl;
        engine.ingest_event(ev);
    };

    // invariant: each filler's only incoming edge is its predecessor at ratio 1, so its surprise is
    // 0 and none is a reservoir candidate.
    for (int rep = 0; rep < 6; ++rep)
        for (int t = 0; t < kFillers; ++t)
            emit("baseline service heartbeat region " + std::to_string(t));

    // invariant: the error templates rank BELOW the band that over-subscribes the general pool, so
    // only the reserve can retain them.
    // invariant: there are more error templates than reserve slots, so the reserve OVERFLOWS --
    // which is the half that proves it bounds rather than merely admits.
    for (int rep = 0; rep < 2; ++rep)
        for (int e = 0; e < kErrorTemplates; ++e)
            emit("batch record " + std::to_string(e) + " rejected by the downstream sink",
                 insight::LogLevel::Error);

    // invariant: the solid spokes are fewer than the free general slots, so they cannot fill the
    // pool alone and the ambiguous group decides the rest.
    for (int rep = 0; rep < 2; ++rep)
        for (int s = 0; s < kSolidSpokes; ++s)
        {
            emit("dispatch loop alpha");
            emit("dispatched task " + std::to_string(s));
        }
    for (int p = 0; p < kSolidSpokes; ++p)
    {
        emit("dispatch loop alpha");
        emit("dispatch loop drain");
    }

    // invariant: each ambiguous spoke is reached by TWO equal-ratio incoming edges, so the
    // most-likely-edge pick is a TIE resolved by preferring more observations.
    // invariant: the reservoir stays FULL either way and only the bag differs, which is why a size
    // assertion alone would be blind and the golden is the oracle.
    for (int rep = 0; rep < 36; ++rep)
    {
        emit("boundary hub x");
        emit("boundary sink x");
    }
    for (int s = 0; s < kAmbiguousSpokes; ++s)
    {
        emit("boundary hub x");
        emit("ambiguous boundary spoke " + std::to_string(s));
    }
    for (int rep = 0; rep < 72; ++rep)
    {
        emit("boundary hub y");
        emit("boundary sink y");
    }
    for (int rep = 0; rep < 2; ++rep)
        for (int s = 0; s < kAmbiguousSpokes; ++s)
        {
            emit("boundary hub y");
            emit("ambiguous boundary spoke " + std::to_string(s));
        }
}

} // namespace insight::metalog::streaming_nearfull

#endif
