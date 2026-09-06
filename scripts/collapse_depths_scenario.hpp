// invariant: one window PAIR whose two cubes sit at DIFFERENT collapse depths, so their diff is
// read at the COARSER of the two -- the collapsed input's axes, never the un-collapsed one's.
// invariant: ingested identically by the in-suite guard and the cross-compiler fixture, so the two
// oracles can never drift onto different windows.
// note: previous reuses the cardinality-explosion scenario rather than re-authoring it.
// refs: DN-42.D18
#ifndef INSIGHT_METALOG_COLLAPSE_DEPTHS_SCENARIO_HPP
#define INSIGHT_METALOG_COLLAPSE_DEPTHS_SCENARIO_HPP

#include "cube_collapse_scenario.hpp"

namespace insight::metalog::collapse_depths
{

// invariant: the same knobs as the scenario it reuses -- the pair is about the cube and its
// collapse depth, so stability and histogram content are off.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    insight::metalog::cube_collapse::configure(cfg);
}

// post: fires the banding guardrail, so the emitted axes carry a band floor.
inline void emit_previous(insight::metalog::MetaLogEngine& engine)
{
    insight::metalog::cube_collapse::emit_window(engine);
}

// post: far under the budget, so this window's axes carry no band floor at all.
inline void emit_current(insight::metalog::MetaLogEngine& engine)
{
    const std::vector<std::string>& comps{insight::metalog::cube_collapse::components()};
    for (std::size_t i = 0; i < 3; ++i)
        for (int rep = 0; rep < 4; ++rep)
        {
            insight::tokenization::CanonicalEvent event;
            event.template_str = "collapse probe";
            event.level = insight::LogLevel::Info;
            event.component = comps[i];
            engine.ingest_event(event);
        }
}

} // namespace insight::metalog::collapse_depths

#endif
