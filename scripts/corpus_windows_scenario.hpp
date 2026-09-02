// corpus_windows_scenario.hpp — the CORPUS window pair: one committed log file, tokenized through
// canon and split into two consecutive MetaLog windows.
//
// It is the only construction in this repo that drives `diff()` and `compose()` over REAL log text
// rather than a synthetic hand-emitted window, and it is now shared by the two oracles that need to
// be looking at the SAME artifact:
//   - scripts/determinism_fixture.cpp          → the cross-compiler bit-identity matrix
//     (determinism_bitidentity.sh), whose digest scripts/spec_conformance_gate.sh schema-judges.
//   - tests/operations/test_golden_vectors.cpp → the committed golden VECTORS (SPEC §12/§13).
//
// WHY SHARED RATHER THAN COPIED. The three claims over this artifact are orthogonal and all three
// are worth having: the digest says it is STABLE across compilers, the conformance gate says it is
// WELL-FORMED against the published schemas, and the vectors say it is RIGHT. Two of those are
// statements about the same bytes only for as long as the two harnesses build those bytes the same
// way, and a copied 25-line ingest body is exactly the kind of thing that drifts by one config
// member and leaves both greens truthful about two different subjects.
//
// WHAT IS FROZEN HERE AND WHY. The three window timestamps are literal epoch offsets, so a document
// emitted from this construction is a function of the corpus bytes alone and of nothing on the
// machine. `emit_stability` is on because the SECOND window is where `stability` (SPEC §5) appears
// at all, and `max_param_histograms = 3` is what makes the per-slot value distributions (§3.5)
// non-empty — without it the emitted `top_k` entries carry no histogram and half of what `diff()`
// computes has nothing to compute over. The split is at the midpoint of the tokenized events, which
// is what gives the two windows overlapping-but-unequal template sets.
//
// Header-only, NO includes: the including TU provides `std` (`<fstream>`, `<string>`, `<vector>`,
// `<optional>`, `<chrono>`), `insight.canon` and `insight.metalog` via `import`/`#include` —
// include this AFTER those. Both includers are plain TUs (not modules), so this is ordinary textual
// inclusion and the imported names resolve.
#ifndef INSIGHT_METALOG_CORPUS_WINDOWS_SCENARIO_HPP
#define INSIGHT_METALOG_CORPUS_WINDOWS_SCENARIO_HPP

namespace insight::metalog::corpus_windows
{

// Sized to hold the whole tokenized corpus; the committed files are tens of lines, so this is
// slack rather than a tuned bound.
inline constexpr std::size_t kArenaBytes{std::size_t{1} << 22};

// The FIXED window axis. Nothing here reads a wall clock — see the header note.
inline constexpr std::int64_t kWindowStartEpochSeconds{1'700'000'000};
inline constexpr std::int64_t kWindowMidEpochSeconds{1'700'000'060};
inline constexpr std::int64_t kWindowEndEpochSeconds{1'700'000'120};

inline void configure(insight::metalog::MetaLogConfig& config)
{
    config.max_param_histograms = 3;
    config.emit_stability = true;
}

// Read a corpus file as lines, dropping a trailing CR so a CRLF checkout produces the same events
// as an LF one (the corpus is text and `.gitattributes` governs it, but a harness that silently
// tokenized a `\r` into the template would make the artifact a function of the checkout).
// `std::nullopt` = the file could not be opened; the caller decides what that costs.
[[nodiscard]] inline std::optional<std::vector<std::string>> read_lines(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        line.clear();
    }
    return lines;
}

// The two consecutive windows. `previous` is the first half of the tokenized events, `current` the
// second; `diff(previous, current)` and `compose(previous, current)` are both defined on them.
struct WindowPair
{
    insight::metalog::MetaLogDocument previous;
    insight::metalog::MetaLogDocument current;
};

// Tokenize + ingest on the caller's engine, so the caller keeps the registry `to_json` needs.
// The arena is local: `MetaLogDocument` owns its strings, so the returned pair outlives it.
//
// THE COMPOSITION IS DELIBERATELY EMPTY (ADR-17's degenerate composition). This construction
// verifies metalog's own reservoir/cube/diff determinism over plain log text, not a CI dialect, and
// metalog links no semantic package; `compose({})` is a defined, runnable core-only state. A
// non-empty set here would make the emitted document a function of a dialect vocabulary this
// package does not ship.
[[nodiscard]] inline WindowPair build(insight::metalog::MetaLogEngine& engine,
                                      const std::vector<std::string>& lines)
{
    namespace tk = insight::tokenization;

    tk::ArenaAllocator arena{kArenaBytes};
    const insight::semantic::ComposedSemantics composed{insight::semantic::compose({})};
    tk::Tokenizer tokenizer{arena, tk::MaskConfig{}, composed};

    std::vector<tk::CanonicalEvent> events;
    events.reserve(lines.size());
    for (const auto& raw : lines)
        if (auto event{tokenizer.process_line(raw)})
            events.push_back(*event);

    using Clock = std::chrono::system_clock;
    const Clock::time_point window_start{std::chrono::seconds{kWindowStartEpochSeconds}};
    const Clock::time_point window_mid{std::chrono::seconds{kWindowMidEpochSeconds}};
    const Clock::time_point window_end{std::chrono::seconds{kWindowEndEpochSeconds}};
    const std::size_t half{events.size() / 2};

    engine.open_window(window_start);
    for (std::size_t i = 0; i < half; ++i)
        engine.ingest_event(events[i]);
    WindowPair pair;
    pair.previous = engine.close_window(window_mid);
    engine.open_window(window_mid);
    for (std::size_t i = half; i < events.size(); ++i)
        engine.ingest_event(events[i]);
    pair.current = engine.close_window(window_end);
    return pair;
}

} // namespace insight::metalog::corpus_windows

#endif // INSIGHT_METALOG_CORPUS_WINDOWS_SCENARIO_HPP
