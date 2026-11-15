// invariant: a synthetic window whose dimensional collapse drops the WHERE axis to depth 0, so the
// emitted where axis carries floor_depth 0 -- the stamp SPEC 16.2 names for a dropped chain.
// invariant: ingested identically by the in-suite guard and the cross-compiler fixture.
// note: every level sits on every component, so LEVEL banding cannot fit until WHERE goes.
// refs: ADR-25.D11, ADR-31.D8
#ifndef INSIGHT_METALOG_WHERE_DROP_SCENARIO_HPP
#define INSIGHT_METALOG_WHERE_DROP_SCENARIO_HPP

namespace insight::metalog::where_drop
{

inline constexpr std::size_t kComponents{2000};

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
        out.reserve(kComponents);
        for (std::size_t i = 0; i < kComponents; ++i)
            out.push_back("svc_" + std::to_string(i));
        return out;
    }();
    return comps;
}

// post: one window of kComponents components at all six levels, whose collapse drops WHERE.
// pre: the caller brackets this with open_window and close_window.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    for (const auto& comp : components())
        for (const insight::LogLevel level :
             {insight::LogLevel::Trace, insight::LogLevel::Debug, insight::LogLevel::Info,
              insight::LogLevel::Warn, insight::LogLevel::Error, insight::LogLevel::Fatal})
        {
            insight::tokenization::CanonicalEvent event;
            event.template_str = "where drop probe";
            event.level = level;
            event.component = comp;
            engine.ingest_event(event);
        }
}

} // namespace insight::metalog::where_drop

#endif
