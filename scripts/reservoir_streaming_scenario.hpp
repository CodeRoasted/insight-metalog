// reservoir_streaming_scenario.hpp — the SECOND ADR-31.D8 near-full reservoir arm, at the tuple
// the STREAMING surface actually ships.
//
// Ingested IDENTICALLY by both determinism oracles, exactly like the sibling M=128 arm:
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite non-hollowness guard.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc×clang × -O{0,3} × -ffp-contract{off,fast}) and the five-leg
//     golden.yaml compare (gcc/clang × x86/arm64 + msvc).
//
// WHY A SECOND ARM. reservoir_nearfull_scenario.hpp anchors the ADR-31.D8 proof at ONE tuple —
// `top_k 64 / M 128 / cap 0 / reserve 0`, the Sift BATCH diff. The streaming surface ships
// `top_k 128 / M 64 / cap 0 / reserve 16` (coderoast-server default_insight_pipeline_config, the
// profile `salience-1/k128-m64-c0-e16`), which is a DIFFERENT candidate population, a DIFFERENT
// admit/evict boundary, and — the half the batch arm has no opinion on at all — a live error-class
// RESERVE. `MEM:naming-a-class-does-not-immunize-you-against-it`: a seam fix proves only the arm it
// was measured on. Without this file the flagship determinism proof never runs at the configuration
// we ship.
//
// WHY NOT "the same window, a different config". Measured, not assumed: replaying the M=128 window
// under `top_k 128` pulls all 24 ambiguous-tie spokes ABOVE the top_k cut, so they stop being
// reservoir candidates and the arm goes hollow — it would fill M=64 with unanimous strong-off-path
// spokes and prove nothing about the boundary. The window below is re-dimensioned so the boundary
// lands where the hazard lives.
//
// WHAT THIS WINDOW IS BUILT TO MAKE FALSE (both directions are asserted in the in-suite guard):
//   • the RESERVE is load-bearing. The general pool is over-subscribed by 64 candidates at the top
//     salience band (8100) for 48 free slots, so with `reserve = 0` NOT ONE error-class template
//     would be retained. `reserve = 16` puts exactly 16 in. That is ADR-20.D7's guarantee —
//     "non-failure salience can NEVER evict a real failure from a high-cardinality window" —
//     measured rather than designed.
//   • the equal-ratio EDGE TIE-BREAK is load-bearing. 40 solid spokes + 24 ambiguous spokes share
//     the 8100 band for 48 free slots, so at least 8 ambiguous spokes MUST be admitted and at least
//     16 of the band MUST be rejected. Lose the `count > best_c` tie-break at engine.cpp's
//     most-likely-edge pick and every ambiguous spoke collapses from surprise 90 to 0 (salience
//     8100 → 3600, novelty only). MEASURED under exactly that mutation: the 8 vacated general slots
//     go to the error class's own overflow — `error_class` 16 → 24, `ambiguous` 8 → 0 — while
//     `reservoir.size()` stays at M. The bag, hence the emitted document, moves; the size does not.
//     That is the ADR-31.D8 leak's exact shape, and the reason a size assertion alone is blind.
//
// The retention tuple is the shipped one; the non-retention knobs (histograms off, stability off,
// the default ngram_size 2) are held at the sibling arm's focused values so the two arms differ in
// the RETENTION TUPLE and the window alone. This file is a retention oracle, not a production
// mirror — the production mirror is the precision gate (playground scenario 08).
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports. Both includers are plain TUs (not modules), so
// this is ordinary textual inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_RESERVOIR_STREAMING_SCENARIO_HPP
#define INSIGHT_METALOG_RESERVOIR_STREAMING_SCENARIO_HPP

namespace insight::metalog::streaming_nearfull
{

// The shipped streaming retention tuple (coderoast-server default_insight_pipeline_config →
// `salience-1/k128-m64-c0-e16`). cap = 0 is the shipped value and is assigned rather than
// inherited: an unstated member of a four-member tuple is how a measurement ends up at half of
// production.
inline constexpr std::size_t kTopK{128};
inline constexpr std::size_t kReservoir{64};
inline constexpr std::size_t kPerKindCap{0};
inline constexpr std::size_t kErrorReserve{16};

// The window's population, spelled as constants because the in-suite guard asserts arithmetic over
// them and a silent re-dimensioning must red rather than shift the boundary.
inline constexpr int kFillers{122}; // count 6 each — with the 6 hubs/sinks that is exactly kTopK
inline constexpr int kErrorTemplates{24};  // > kErrorReserve, so the reserve OVERFLOWS
inline constexpr int kSolidSpokes{40};     // strong off-path (band 90); < the 48 free general slots
inline constexpr int kAmbiguousSpokes{24}; // the equal-ratio tie, at the boundary

// Free general-pool slots once the error reserve is served: M - reserve.
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

// Drive one window whose reservoir fills to the full M=64 with BOTH the error reserve and the
// equal-ratio edge tie-break deciding membership. Caller does open_window / close_window around
// this. Deterministic given a fixed engine config (configure() above).
//
// The arithmetic, so a later reader can re-derive the boundary instead of trusting it. 1380 lines,
// 216 distinct templates (both pinned by the emitted document). Every salient template is seen 2 or
// 3 times, so `count·100 < lines` holds and `count·1000 < lines` does not for any of them: the
// rarity modulator is a UNIFORM 90 and the ranking is the severity band alone.
//
//   group          | n   | count | band                       | salience | fate
//   ---------------|-----|-------|----------------------------|----------|-------------------------
//   fillers        | 122 |     6 | none (severity 0)          |        0 | top_k, not a candidate
//   hubs + sinks   |   6 | 36-120| none                       |        0 | top_k, not a candidate
//   solid spokes   |  40 |     2 | surprise 90 (2/120 < 2%)   |     8100 | general pool
//   ambiguous      |  24 |     3 | surprise 90 via the TIE    |     8100 | the boundary cuts here
//   error class    |  24 |     2 | level Error 80             |     7200 | reserve only (16 of 24)
//
// 122 fillers + 6 hubs/sinks = 128 = top_k exactly, so the cut falls on a count boundary (6 vs 3)
// and never inside a tie group. 40 + 24 = 64 candidates at 8100 for 48 free slots, so the general
// pool is filled by that band ALONE and the error class reaches the document only through the
// reserve. 88 candidates for 64 slots — the window is over-subscribed at every tier.
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

    // (1) Top-K fillers: benign Info templates present from the window start (novelty 0) and
    //     frequent enough to hold the top_k cut. Each filler's only incoming edge is its
    //     predecessor at ratio 1 → surprise 0, so severity is 0 and none is a reservoir candidate.
    for (int rep = 0; rep < 6; ++rep)
        for (int t = 0; t < kFillers; ++t)
            emit("baseline service heartbeat region " + std::to_string(t));

    // (2) The error class: 24 rare Error templates in a round-robin, so each one's most-likely
    //     incoming edge is its predecessor at ratio 1 (surprise 0) and its salience is the level
    //     band alone — 80 × 90 = 7200, BELOW the 8100 band that over-subscribes the general pool.
    //     Only the reserve can retain them, and 24 > 16 makes it overflow, which is the half that
    //     proves the reserve bounds rather than merely admits.
    for (int rep = 0; rep < 2; ++rep)
        for (int e = 0; e < kErrorTemplates; ++e)
            emit("batch record " + std::to_string(e) + " rejected by the downstream sink",
                 insight::LogLevel::Error);

    // (3) Solid strong-off-path spokes: one busy hub fans out to 40 distinct rare spokes (each
    //     emitted TWICE so the >=2-observation surprise floor is met), then the hub is padded to an
    //     outgoing total of 120 against a drain. Each spoke's only incoming edge is hub->spoke at
    //     2/120 < 2% → band 90, with NO level severity. 40 of them, against 48 free slots — so they
    //     cannot fill the general pool alone, and the ambiguous group below decides the rest.
    for (int rep = 0; rep < 2; ++rep)
        for (int s = 0; s < kSolidSpokes; ++s)
        {
            emit("dispatch loop alpha");
            emit("dispatched task " + std::to_string(s));
        }
    for (int p = 0; p < kSolidSpokes; ++p) // pads hub outgoing 80 -> 120, the band-90 threshold
    {
        emit("dispatch loop alpha");
        emit("dispatch loop drain");
    }

    // (4) Ambiguous boundary spokes: each reached by TWO EQUAL-RATIO incoming edges — X->Sa once
    //     (count 1, below the >=2 observation floor → surprise 0 if it wins) and Y->Sa twice
    //     (count 2 → band 90 if it wins). Equal ratio (1/60 == 2/120, with Yout = 2·Xout) makes the
    //     most-likely-edge pick a TIE, resolved by engine.cpp's "prefer MORE observations" rule.
    //     Resolve it by unordered_map order instead — the ADR-31.D8 defect — and clang and gcc pick
    //     different edges, the spoke's structural_surprise moves, and its 8100 seat falls to the
    //     error class's reserve overflow. The reservoir stays FULL either way; only the bag
    //     differs, which is precisely why a size assertion alone would be blind and the golden is
    //     the oracle.
    for (int rep = 0; rep < 36; ++rep) // Hub X: outgoing 60 = 24 spoke edges + 36 drain edges
    {
        emit("boundary hub x");
        emit("boundary sink x");
    }
    for (int s = 0; s < kAmbiguousSpokes; ++s)
    {
        emit("boundary hub x");
        emit("ambiguous boundary spoke " + std::to_string(s));
    }
    for (int rep = 0; rep < 72; ++rep) // Hub Y: outgoing 120 = 48 spoke edges + 72 drain edges
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

#endif // INSIGHT_METALOG_RESERVOIR_STREAMING_SCENARIO_HPP
