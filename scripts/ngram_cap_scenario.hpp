// ngram_cap_scenario.hpp — the shared SPEC §4 `dropped_ngram_observations` scenario.
//
// Ingested IDENTICALLY by both determinism oracles so they exercise the EXACT same accounting
// bound (no drift between them):
//   - tests/determinism/test_determinism_gate.cpp  → the in-suite non-hollow guard.
//   - scripts/determinism_fixture.cpp               → the cross-compiler matrix
//     (determinism_bitidentity.sh: gcc x clang x -O{0,3} x -ffp-contract{off,fast}) and the
//     five golden.yaml legs, including the MSVC one.
//
// WHY THIS SCENARIO EXISTS, and it is a gate-coverage fact rather than a new engine risk.
// `behavior.dropped_ngram_observations` landed on 2026-08-24 and the bound it reports BINDS on
// the real stream — 563 of 34 506 GitHub windows at the cut Sift embeds, and up to 92.9 % of a
// GitLab window's bigrams refused. The committed determinism corpus never binds it: every one of
// its documents stays under `max_ngram_keys`, so §4's absence-means-zero encoding makes the field
// ABSENT in all of them and every standing gate — the byte-identity sweep and the spec's own §8
// validator alike — has only ever judged documents in which the key does not appear. A field that
// no gate has seen POPULATED is a field whose emission, ordering and schema conformance are
// unproven at the only grain that ships. This scenario is the document that carries it.
//
// THE ARITHMETIC IS THE ORACLE, and it is exact rather than approximate. `kDistinctTemplates`
// one-shot templates emitted in sequence form `kDistinctTemplates - 1` consecutive bigrams, every
// one of them a NEW key. The first `max_ngram_keys` fill the table and every later one is refused
// and counted, so the emitted count is `kDistinctTemplates - 1 - max_ngram_keys` = 1903 at the
// shipped default cap of 4096. OBSERVATIONS, never distinct keys.
//
// A SECOND SURFACE THIS DRIVES, for free and worth naming so a later reader does not "simplify"
// the fixture: with every template at count 1 and every admitted bigram at count 1, BOTH top-K
// selects run entirely on their tie-breaks — 6000 candidates ranked by `template_id` for
// `stats.top_k`, and 4096 candidates ranked by `sequence` for `behavior.top_ngrams`. A tie-break
// that stopped being a total order would surface here as a cross-leg byte difference and nowhere
// else in this corpus.
//
// Header-only, NO includes: the including TU provides `std`, `insight.canon` and `insight.metalog`
// via `import` — include this AFTER those imports. Both includers are plain TUs (not modules), so
// this is ordinary textual inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_NGRAM_CAP_SCENARIO_HPP
#define INSIGHT_METALOG_NGRAM_CAP_SCENARIO_HPP

namespace insight::metalog::ngram_cap
{

// The stream's size. 6000 one-shot templates -> 5999 bigrams.
inline constexpr std::size_t kDistinctTemplates{6000};

// The cut the wedge product ships: `sift` builds a bare PipelineConfig and sets neither
// `metalog.ngram_size` nor `metalog.max_ngram_keys`, so the producer defaults ARE Sift's
// configuration — ngram_size 2, max_ngram_keys 4096. Left at their defaults here on purpose: a
// literal would silently stop tracking the shipped cut the day a default moves, and the emitted
// count below is derived from the config rather than hard-coded for exactly that reason.
inline void configure(insight::metalog::MetaLogConfig& cfg)
{
    cfg.max_param_histograms = 0; // the synthetic stream carries no params
    cfg.emit_stability = false;   // one window, so there is no prior to compare against
}

// The count the emitted document must declare, derived from the config rather than asserted as a
// literal — `kDistinctTemplates - 1` bigrams, `max_ngram_keys` admitted, the rest refused.
//
// SATURATING, and the saturation is load-bearing rather than defensive. A cap at or above the
// bigram count refuses nothing, and this is unsigned arithmetic: the subtraction would wrap to a
// number in the quintillions and the guard's failure message — the message whose whole job is to
// say "THE SECTION WENT HOLLOW" — would print garbage in precisely the case it exists to catch.
[[nodiscard]] inline std::uint64_t
expected_dropped_observations(const insight::metalog::MetaLogConfig& cfg)
{
    constexpr std::size_t kBigrams{kDistinctTemplates - 1};
    return kBigrams > cfg.max_ngram_keys ? static_cast<std::uint64_t>(kBigrams - cfg.max_ngram_keys)
                                         : 0U;
}

// Drive one window whose n-gram accounting bound BINDS. Caller does open_window / close_window
// around this. Deterministic given a fixed engine config (configure() above).
//
// Templates are fed as already-canonical strings, exactly as the sibling scenarios do — this
// fixture never runs canon, so the digit discriminators below are not at risk of being masked
// into one shape. (A stream that DOES go through canon needs alphabetic discriminators; that is
// the pipeline suite's problem, not this one's.)
inline void emit_window(insight::metalog::MetaLogEngine& engine)
{
    insight::tokenization::CanonicalEvent event;
    for (std::size_t index{0}; index < kDistinctTemplates; ++index)
    {
        // Held in a local for the duration of the ingest call: `template_str` is a view.
        const std::string tmpl{"cap probe stage " + std::to_string(index) + " completed"};
        event.template_str = tmpl;
        event.level = insight::LogLevel::Info;
        engine.ingest_event(event);
    }
}

} // namespace insight::metalog::ngram_cap

#endif // INSIGHT_METALOG_NGRAM_CAP_SCENARIO_HPP
