// invariant: one window that fills the item reservoir to the full M and decides the admit/evict
// boundary on structural surprise.
// invariant: ingested identically by the in-suite golden and the cross-compiler fixture.
// refs: ADR-31.D8
#ifndef INSIGHT_METALOG_RESERVOIR_NEARFULL_SCENARIO_HPP
#define INSIGHT_METALOG_RESERVOIR_NEARFULL_SCENARIO_HPP

namespace insight::metalog::nearfull
{

// invariant: the production batch-diff reservoir size, with the per-kind cap at 0.
// note: the production per-kind cap would hard-ceiling the reservoir below M, never reaching it.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.top_k_size = 64;
    cfg.reservoir_size = 128;
    cfg.reservoir_per_kind_cap = 0;
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// post: one window whose reservoir fills to M with the boundary decided by the non-deterministic
// input this scenario exists to pin.
// pre: the caller brackets this with open_window and close_window.
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

    // invariant: frequent benign templates present from the window start, admitted to top-K by
    // count, so none is ever a reservoir candidate.
    constexpr int kTopKFillers{64};
    for (int rep = 0; rep < 6; ++rep)
        for (int t = 0; t < kTopKFillers; ++t)
            emit("baseline service heartbeat region " + std::to_string(t));

    // invariant: a busy hub fans out to distinct rare spokes, each emitted twice so the
    // two-observation surprise floor is met; a small edge ratio gives a high surprise band.
    const auto hub_spokes = [&](const std::string& hub, int fanout, int spoke_base)
    {
        for (int rep = 0; rep < 2; ++rep)
            for (int s = 0; s < fanout; ++s)
            {
                emit(hub);
                emit("dispatched task " + std::to_string(spoke_base + s));
            }
    };
    hub_spokes("dispatch loop alpha", 110, 0);
    hub_spokes("dispatch loop beta", 40, 10'000);

    // invariant: each ambiguous spoke is reached by TWO equal-ratio incoming edges, so the
    // most-likely-edge pick is a TIE and the spoke's surprise depends on how it resolves.
    // invariant: oversubscribed, so the reservoir stays FULL on both standard libraries while the
    // BAG membership differs -- which is why a size assertion alone would be blind.
    constexpr int kAmbiguous{24};
    for (int rep = 0; rep < 36; ++rep)
    {
        emit("hub x");
        emit("sink x");
    }
    for (int rep = 0; rep < 72; ++rep)
    {
        emit("hub y");
        emit("sink y");
    }
    for (int s = 0; s < kAmbiguous; ++s)
    {
        emit("hub x");
        emit("ambiguous boundary spoke " + std::to_string(s));
    }
    for (int rep = 0; rep < 2; ++rep)
        for (int s = 0; s < kAmbiguous; ++s)
        {
            emit("hub y");
            emit("ambiguous boundary spoke " + std::to_string(s));
        }
}

} // namespace insight::metalog::nearfull

#endif
