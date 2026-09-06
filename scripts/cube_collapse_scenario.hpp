// invariant: a synthetic cardinality-explosion window that FIRES the dimensional-collapse guardrail
// -- the closed cube exceeds the budget and the level banding collapses it.
// invariant: ingested identically by the in-suite guardrail tests and the cross-leg gate.
// note: the axis-selection tie-break is content, so a leg must replay a window that collapses.
// refs: ADR-31.D8
#ifndef INSIGHT_METALOG_CUBE_COLLAPSE_SCENARIO_HPP
#define INSIGHT_METALOG_CUBE_COLLAPSE_SCENARIO_HPP

namespace insight::metalog::cube_collapse
{

// invariant: stability and histograms are off, so the digest stays about the cube.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.top_k_size = 16;
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// invariant: static storage, so the CanonicalEvent string_views stay valid.
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

// post: one window that explodes the cube past the budget, so the banding fires.
// pre: the caller brackets this with open_window and close_window.
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

#endif
