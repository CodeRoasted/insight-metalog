// reservoir_nearfull_scenario.hpp — the shared ADR-31.D8 near-full reservoir scenario.
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same M=128
// admit/evict boundary (no drift between them):
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite frozen-SHA golden.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc×clang × -O{0,3} × -ffp-contract{off,fast}).
//
// ADR-31.D8 (bibles/determinism_model.md): the item-reservoir (§2.11) salience inputs —
// structural_surprise above all — were not bit-identical clang≢gcc, and an order-dependent
// most-likely-edge pick flipped a near-tie admit/evict at the M=128 boundary → a different bag →
// a Tier-1 violation in the production Sift batch-diff (eidos ships reservoir_size=128). The prior
// golden never drove the reservoir to that boundary, so it was blind to the leak.
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports. Both includers are plain TUs (not modules), so
// this is ordinary textual inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_RESERVOIR_NEARFULL_SCENARIO_HPP
#define INSIGHT_METALOG_RESERVOIR_NEARFULL_SCENARIO_HPP

namespace insight::metalog::nearfull
{

// Production Sift batch-diff reservoir (diff.api-config kDefaultIngestReservoirSize=128). cap=0
// (Founder ruling 2026-06-14): the per-kind cap keys on (StructuralRole×LogLevel)=4×7=28 kinds, so
// the production cap=4 would hard-ceiling the reservoir at 112 — it can never reach 128. cap=0
// admits the top-M by pure salience rank: the clean oracle for the salience VALUE, the ADR-31.D8
// root, which is upstream of the admission cap.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.top_k_size = 64;
    cfg.reservoir_size = 128;
    cfg.reservoir_per_kind_cap = 0;
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// Drive one window that fills the item-reservoir to the full M=128 with the admit/evict boundary
// decided by structural_surprise — the ADR-31.D8 non-deterministic input. Caller does open_window /
// close_window around this. Deterministic given a fixed engine config (configure() above).
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    const auto emit =
        [&engine](const std::string& tmpl, insight::LogLevel lvl = insight::LogLevel::Info)
    {
        insight::tokenization::CanonicalEvent ev;
        ev.template_str = tmpl; // view into `tmpl`, valid for the duration of this ingest call
        ev.level = lvl;
        engine.ingest_event(ev);
    };

    // (1) Top-K fillers: 64 frequent benign Info templates, present from the window start
    //     (novelty 0) and frequent (admitted to Top-K by count → never reservoir candidates).
    constexpr int kTopKFillers{64};
    for (int rep = 0; rep < 6; ++rep)
        for (int t = 0; t < kTopKFillers; ++t)
            emit("baseline service heartbeat region " + std::to_string(t));

    // (2) Solid structural-surprise spokes: a busy hub fans out to many distinct rare spokes (each
    //     emitted TWICE so the >=2-observation surprise floor is met). Each spoke's only incoming
    //     edge is hub->spoke at ratio 2/outgoing(hub); a small ratio → a high structural_surprise
    //     band → the benign Info spoke is salient with NO level severity. These deterministically
    //     fill most of M.
    const auto hub_spokes = [&](const std::string& hub, int fanout, int spoke_base)
    {
        for (int rep = 0; rep < 2; ++rep)
            for (int s = 0; s < fanout; ++s)
            {
                emit(hub);
                emit("dispatched task " + std::to_string(spoke_base + s));
            }
    };
    hub_spokes("dispatch loop alpha", 110, 0); // outgoing 220, ratio 2/220<2% → band 90 (solid)
    hub_spokes("dispatch loop beta", 40,
               10'000); // outgoing 80,  ratio 2/80=2.5% → band 75 (boundary)

    // (3) Ambiguous boundary spokes: each reached by TWO equal-ratio incoming edges — X->Sa once
    //     (count 1, below the >=2 floor → surprise 0 if it wins) and Y->Sa twice (count 2 → a real
    //     surprise band if it wins). Equal ratio (1/Xout == 2/Yout, with Yout=2*Xout) makes the
    //     most-likely-edge pick a TIE: ADR-31.D8 resolves it by unordered_map order → clang picks
    //     one, gcc the other → the spoke's structural_surprise (hence whether it clears the M=128
    //     cutoff) diverges. Oversubscribed so the reservoir stays full (size==M) on both stdlibs
    //     while the BAG membership differs → the digest differs → the leak surfaces.
    constexpr int kAmbiguous{24};
    // Hub X: outgoing 60 (X->Sa once each = 24, plus 36 padding to a sink).
    // Hub Y: outgoing 120 = 2*Xout (Y->Sa twice each = 48, plus 72 padding) → 1/60 == 2/120 tie.
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

#endif // INSIGHT_METALOG_RESERVOIR_NEARFULL_SCENARIO_HPP
