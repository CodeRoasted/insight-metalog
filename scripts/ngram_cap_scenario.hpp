// invariant: the one window here whose bigram stream OVERRUNS the key cap, so the emitted document
// CARRIES the dropped-observations field instead of omitting it.
// invariant: ingested identically by the in-suite guard and the cross-compiler fixture.
// note: OBSERVATIONS, never distinct keys.
// refs: ADR-9.D3
#ifndef INSIGHT_METALOG_NGRAM_CAP_SCENARIO_HPP
#define INSIGHT_METALOG_NGRAM_CAP_SCENARIO_HPP

namespace insight::metalog::ngram_cap
{

// invariant: one-shot templates emitted in sequence, so the stream forms one fewer bigram than it
// has templates and every bigram is a new key.
inline constexpr std::size_t kDistinctTemplates{6000};

// invariant: the producer defaults ARE the shipped cut's configuration, so they are left unset here
// and the expected count below is derived from the config, never a literal.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.max_param_histograms = 0;
    cfg.emit_stability = false;
}

// post: the count the emitted document must declare, derived from the config.
// post: SATURATING -- a cap at or above the bigram count refuses nothing, and unsigned wrap would
// print garbage in exactly the case the guard exists to catch.
[[nodiscard]] inline std::uint64_t
expected_dropped_observations(const insight::metalog::MetaLogConfig& cfg)
{
    constexpr std::size_t kBigrams{kDistinctTemplates - 1};
    return kBigrams > cfg.max_ngram_keys ? static_cast<std::uint64_t>(kBigrams - cfg.max_ngram_keys)
                                         : 0U;
}

// post: one window whose n-gram accounting bound BINDS.
// pre: the caller brackets this with open_window and close_window.
// invariant: templates are already canonical and never run through canon, so the digit
// discriminators are not at risk of being masked into one shape.
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    insight::tokenization::CanonicalEvent event;
    for (std::size_t index{0}; index < kDistinctTemplates; ++index)
    {
        // invariant: held in a local for the ingest call, since template_str is a view.
        const std::string tmpl{"cap probe stage " + std::to_string(index) + " completed"};
        event.template_str = tmpl;
        event.level = insight::LogLevel::Info;
        engine.ingest_event(event);
    }
}

} // namespace insight::metalog::ngram_cap

#endif
