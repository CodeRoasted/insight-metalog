// invariant: one span window that forces the over-cap top-K select and cuts the cap THROUGH a
// weight tie, so the surviving edge rides the canonical-key tie-break alone.
// invariant: ingested identically by the in-suite guard and the cross-compiler fixture.
// refs: ADR-29.D2, ADR-31.D8, SRC-D-OTEL-21
#ifndef INSIGHT_METALOG_SERVICE_EDGES_OVERCAP_SCENARIO_HPP
#define INSIGHT_METALOG_SERVICE_EDGES_OVERCAP_SCENARIO_HPP

namespace insight::metalog::service_edges_overcap
{

// invariant: the cap sits below the number of distinct edges, so the over-cap select path is taken;
// max_active_spans holds every span, so no parent edge is orphaned.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.max_service_edges = 3;
    cfg.max_active_spans = 64;
    cfg.emit_stability = false;
    cfg.max_param_histograms = 0;
}

// post: one window of spans building exactly five caller-to-callee edges.
// invariant: only the component drives the service topology; templates are per-role.
// pre: the caller brackets this with open_window and close_window.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    std::uint64_t next_span_id{1};
    const auto edge = [&](const std::string& caller, const std::string& callee, int weight)
    {
        const std::uint64_t parent{next_span_id++};
        {
            insight::tokenization::CanonicalEvent ev;
            ev.template_str = caller;
            ev.component = caller;
            ev.trace.present = true;
            ev.trace.is_span = true;
            ev.trace.span_id = insight::SpanId{parent};
            engine.ingest_event(ev);
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

    // invariant: the weights make the last cap slot a multi-way tie, so canonical-ascending order
    // among the tied keys decides which edge survives and the rest are counted dropped.
    edge("api", "db", 5);
    edge("api", "cache", 4);
    edge("gateway", "auth", 2);
    edge("gateway", "billing", 2);
    edge("worker", "queue", 2);
}

} // namespace insight::metalog::service_edges_overcap

#endif
