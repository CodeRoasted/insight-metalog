// cube_collapse_scenario.hpp — the shared synthetic cardinality-explosion window for the cube
// dimensional-collapse guardrail (cube_perf_and_collapse.md §C3).
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same collapse:
//   - tests/cube/test_cube.cpp        → the in-suite behavioral guardrail tests.
//   - scripts/determinism_fixture.cpp → the cross-compiler/leg determinism gate
//     (determinism_bitidentity.sh + golden.yaml, --cube-collapse section).
//
// The always-on cube can explode (O(B·2ⁿ)); the per-window guardrail bounds it by coarsening the base
// + re-closing. The axis-selection tie-break is an F5-M8-class content decision (a declared total
// order), so the cross-leg gate MUST replay a window that actually FIRES a collapse — else the
// collapse policy is unproven cross-machine (the F5-M8 oracle-coverage lesson). This window has 1500
// distinct components each at two bandable levels (Trace/Debug): the closed cube exceeds the 4096-cell
// budget and the LEVEL interval-banding {Trace,Debug}→Debug fires (band_floor=2), WHERE kept intact.
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports (ordinary textual inclusion, imported names resolve).
#ifndef INSIGHT_METALOG_CUBE_COLLAPSE_SCENARIO_HPP
#define INSIGHT_METALOG_CUBE_COLLAPSE_SCENARIO_HPP

namespace insight::metalog::cube_collapse
{

// Keep the digest focused on the always-on cube + its collapse (no stability/histogram noise).
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.top_k_size = 16;
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// The distinct components, in static storage so the CanonicalEvent string_views stay valid.
inline const std::vector<std::string>& components()
{
    static const std::vector<std::string> comps = []
    {
        std::vector<std::string> out;
        out.reserve(1500);
        for (int i = 0; i < 1500; ++i)
            out.push_back("svc_" + std::to_string(i));
        return out;
    }();
    return comps;
}

// Drive ONE window that explodes the cube past the budget so the {Trace,Debug}→Debug banding fires.
// Caller does open_window / close_window around this. Deterministic given configure() above.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    for (const auto& comp : components())
    {
        insight::tokenization::CanonicalEvent trace;
        trace.template_str = "collapse probe";
        trace.level = insight::LogLevel::Trace;
        trace.component = comp;
        engine.ingest_event(trace);

        insight::tokenization::CanonicalEvent debug;
        debug.template_str = "collapse probe";
        debug.level = insight::LogLevel::Debug;
        debug.component = comp;
        engine.ingest_event(debug);
    }
}

} // namespace insight::metalog::cube_collapse

#endif // INSIGHT_METALOG_CUBE_COLLAPSE_SCENARIO_HPP
