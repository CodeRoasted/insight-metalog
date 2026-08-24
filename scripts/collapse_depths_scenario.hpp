// collapse_depths_scenario.hpp — the shared window PAIR whose two cubes sit at DIFFERENT collapse
// depths, so their diff is read at the §16.10 minimal common collapse.
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same compare-at-min:
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite non-hollowness guard.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc×clang × -O{0,3} × -ffp-contract{off,fast}),
//     --collapse-depths.
//
// WHY IT EXISTS (DN-42.D18 property (ii)). §16.10 mandates that a diff of two cubes be read at the
// COARSER of the two on each axis; §13.6's example text carries an unbolded comment saying
// `cube_diff.axes` equals both inputs' `cube.axes`, which is not normative anywhere and which this
// case refutes by construction. A conformance corpus made only of same-depth pairs would go green
// and be blind on precisely the shape the two clauses disagree about — so the shape is a REQUIRED
// witness in the gate's population census, not an optional extra.
//
// THE PAIR. `previous` is the §C3 cardinality explosion (cube_collapse_scenario.hpp, reused rather
// than re-authored so the two scenarios can never drift into two different collapses): 1500
// components at two bandable levels, closing over the 4096-cell budget, so the LEVEL interval
// banding fires and the emitted axes carry `band_floor: 2`. `current` is a handful of Info events
// on three of those same components — far under the budget, so its own axes carry NO band_floor at
// all. `min_common_collapse` takes the MAX band floor, so the diff is read at `band_floor: 2` while
// one of its inputs was never banded: the diff's axes equal neither input's.
//
// The overlap in components is deliberate. Disjoint windows would make the current cube's cells all
// emerge, which is a real diff but a duller one; sharing `svc_0..svc_2` keeps the border about the
// LEVEL collapse rather than about the WHERE labels.
//
// Header-only. The ONE include is the sibling scenario it reuses (which is itself include-free);
// the including TU still provides `std`, `insight.canon` and `insight.metalog` via `import`, so
// include this AFTER those imports.
#ifndef INSIGHT_METALOG_COLLAPSE_DEPTHS_SCENARIO_HPP
#define INSIGHT_METALOG_COLLAPSE_DEPTHS_SCENARIO_HPP

#include "cube_collapse_scenario.hpp"

namespace insight::metalog::collapse_depths
{

// The same knobs as the collapse scenario it reuses: the pair is about the cube and its collapse
// depth, and stability/histogram content would only add noise the other sections already own.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    insight::metalog::cube_collapse::configure(cfg);
}

// PREVIOUS: the cardinality explosion that FIRES the guardrail → axes stamped `band_floor: 2`.
inline void emit_previous(insight::metalog::MetaLogEngine& engine)
{
    insight::metalog::cube_collapse::emit_window(engine);
}

// CURRENT: three components, one level, far under the budget → NO banding, so this window's own
// axes carry no `band_floor` and the pair's collapse states genuinely differ.
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

#endif // INSIGHT_METALOG_COLLAPSE_DEPTHS_SCENARIO_HPP
