// invariant: one order-3 window PAIR with span records whose declared edges share their ids with
// the log trigrams, so a declared edge leaking into top_ngrams would sit beside a trigram there.
// invariant: the declared edge set moves between the windows, so the diff carries a span-edge
// delta with a new and a vanished edge.
// invariant: ingested identically by the in-suite guard and the cross-compiler fixture.
// refs: ADR-24.D7, F-SRC-insight-metalog:engine.cpp:resolve_span_edges
#ifndef INSIGHT_METALOG_SPAN_ORDER3_SCENARIO_HPP
#define INSIGHT_METALOG_SPAN_ORDER3_SCENARIO_HPP

namespace insight::metalog::span_order3
{

inline constexpr std::size_t kNgramSize{3};

// invariant: max_active_spans holds every span of a window, so no parent edge is orphaned.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.ngram_size = kNgramSize;
    cfg.max_active_spans = 64;
    cfg.emit_stability = false;
    cfg.max_param_histograms = 0;
}

// invariant: one window's mix; every span edge has the parent checkout, the id the log trigrams
// (checkout, charge, receipt) and (checkout, charge, retry) open with.
struct WindowMix
{
    int charge_spans{0};
    int notify_spans{0};
    int refund_spans{0};
    int receipt_runs{0};
    int retry_runs{0};
};

// invariant: (checkout, notify) vanishes and (checkout, refund) appears, and both trigram
// conditionals move, so the diff carries a span-edge delta and trigram rate_changed rows.
inline constexpr WindowMix kPrevious{
    .charge_spans = 3, .notify_spans = 1, .refund_spans = 0, .receipt_runs = 2, .retry_runs = 1};
inline constexpr WindowMix kCurrent{
    .charge_spans = 1, .notify_spans = 0, .refund_spans = 2, .receipt_runs = 1, .retry_runs = 2};

// post: one window of span records, each child under its own checkout span, then the log runs.
// pre: the caller brackets this with open_window and close_window.
inline void emit_window(insight::metalog::MetaLogEngine& engine, const WindowMix& mix)
{
    std::uint64_t next_span_id{1};
    const auto span_edge = [&](std::string_view child)
    {
        const std::uint64_t parent{next_span_id++};
        insight::tokenization::CanonicalEvent root;
        root.template_str = "checkout";
        root.level = insight::LogLevel::Info;
        root.trace.present = true;
        root.trace.is_span = true;
        root.trace.span_id = insight::SpanId{parent};
        engine.ingest_event(root);
        insight::tokenization::CanonicalEvent leaf;
        leaf.template_str = child;
        leaf.level = insight::LogLevel::Info;
        leaf.trace.present = true;
        leaf.trace.is_span = true;
        leaf.trace.span_id = insight::SpanId{next_span_id++};
        leaf.trace.has_parent = true;
        leaf.trace.parent_span_id = insight::SpanId{parent};
        engine.ingest_event(leaf);
    };
    const auto log_run = [&](std::string_view last)
    {
        for (const std::string_view templ :
             {std::string_view{"checkout"}, std::string_view{"charge"}, last})
        {
            insight::tokenization::CanonicalEvent event;
            event.template_str = templ;
            event.level = insight::LogLevel::Info;
            engine.ingest_event(event);
        }
    };

    for (int index{0}; index < mix.charge_spans; ++index)
        span_edge("charge");
    for (int index{0}; index < mix.notify_spans; ++index)
        span_edge("notify");
    for (int index{0}; index < mix.refund_spans; ++index)
        span_edge("refund");
    for (int index{0}; index < mix.receipt_runs; ++index)
        log_run("receipt");
    for (int index{0}; index < mix.retry_runs; ++index)
        log_run("retry");
}

} // namespace insight::metalog::span_order3

#endif
