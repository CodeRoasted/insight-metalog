// service_edges_overcap_scenario.hpp — the shared O4b service-topology over-cap scenario.
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same service_edges
// emission — including its ONE non-trivial branch, the over-cap top-K select — with no drift:
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite value/non-hollowness guard.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc×clang × -O{0,3} × -ffp-contract{off,fast}).
//
// O4b (insight_otel_epic.md §13.7, D-OTEL-21): resolve_span_edges distils each declared span
// parent→child into a (caller_component → callee_component) integer-weighted edge, then
// build_service_edges emits the top `max_service_edges` by weight with a canonical-key tie-break,
// re-sorted into canonical (caller, callee) order for the wire, with dropped_edges = the honest
// truncation. The block was shipped "deterministic by construction" (std::map canonical order,
// integer weights, a strict-total-order select comparator) — but NO determinism golden exercised
// it, so the claim was never *proven* across stdlib/ISA/OS. This scenario closes that: it forces
// the over-cap path and cuts the cap THROUGH a 3-way weight tie, so the surviving 3rd edge is
// decided PURELY by the canonical-key tie-break. If that select were stdlib-order-dependent (the
// F5-M8 hazard class), the surviving edge — hence the whole block's bytes — would flip clang≢gcc.
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports. Both includers are plain TUs (not modules), so
// this is ordinary textual inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_SERVICE_EDGES_OVERCAP_SCENARIO_HPP
#define INSIGHT_METALOG_SERVICE_EDGES_OVERCAP_SCENARIO_HPP

namespace insight::metalog::service_edges_overcap
{

// Cap the emitted block at 3 while the scenario builds 5 distinct edges → the over-cap top-K select
// path is taken (dropped_edges = 2). max_active_spans holds every scenario span (no FIFO eviction →
// no orphan), and the single-window config keeps the doc minimal & prev-window-state-free.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.max_service_edges = 3; // below the 5 distinct edges → forces the over-cap top-K select
    cfg.max_active_spans = 64; // > the 20 spans below → no eviction, no orphaned parent edge
    cfg.emit_stability = false;
    cfg.max_param_histograms = 0;
}

// Emit one window of spans that build exactly five (caller → callee) service edges. Each edge()
// call emits one parent span (its component = caller) followed by `weight` child spans (component =
// callee) that declare the parent → +weight to service_edges_[{caller, callee}]. Templates are
// per-role strings; ONLY the component drives the service topology. Caller does open_window /
// close_window around this. Deterministic given configure() above.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    std::uint64_t next_span_id{1};
    const auto edge = [&](const std::string& caller, const std::string& callee, int weight)
    {
        const std::uint64_t parent{next_span_id++};
        {
            insight::tokenization::CanonicalEvent ev;
            ev.template_str = caller; // view into `caller`, valid for this edge() full-expression
            ev.component = caller;
            ev.trace.present = true;
            ev.trace.is_span = true;
            ev.trace.span_id = insight::SpanId{parent};
            engine.ingest_event(ev); // record_span COPIES component into span_templates_
        }
        for (int i = 0; i < weight; ++i)
        {
            insight::tokenization::CanonicalEvent ev;
            ev.template_str = callee;
            ev.component = callee;
            ev.trace.present = true;
            ev.trace.is_span = true;
            ev.trace.span_id = insight::SpanId{next_span_id++};
            ev.trace.has_parent = true;
            ev.trace.parent_span_id = insight::SpanId{parent};
            engine.ingest_event(ev);
        }
    };

    // Weights chosen so the 3-slot cap cuts THROUGH a 3-way weight-2 tie. Canonical-ascending among
    // the tied keys: {gateway,auth} < {gateway,billing} < {worker,queue}; the top-K keeps the
    // smallest → {gateway,auth} survives on the tie-break alone, {gateway,billing}+{worker,queue}
    // drop (dropped_edges = 2). The surviving wire, re-sorted canonical: {api,cache}=4, {api,db}=5,
    // {gateway,auth}=2.
    edge("api", "db", 5);          // {api,db}          w=5  — clear top
    edge("api", "cache", 4);       // {api,cache}       w=4  — clear 2nd
    edge("gateway", "auth", 2);    // {gateway,auth}    w=2  ┐
    edge("gateway", "billing", 2); // {gateway,billing} w=2  ├ 3-way tie for the 3rd (last) slot
    edge("worker", "queue",
         2); // {worker,queue}    w=2  ┘  → canonical key-asc keeps {gateway,auth}
}

} // namespace insight::metalog::service_edges_overcap

#endif // INSIGHT_METALOG_SERVICE_EDGES_OVERCAP_SCENARIO_HPP
