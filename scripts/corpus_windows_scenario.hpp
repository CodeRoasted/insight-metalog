// invariant: the only construction in this repo that drives diff() and compose() over REAL
// tokenized log text rather than a synthetic hand-emitted window.
// invariant: shared by every oracle that replays it rather than copied into each, so they all
// judge the same bytes.
// pre: the including TU has already imported std, insight.canon and insight.metalog.
#ifndef INSIGHT_METALOG_CORPUS_WINDOWS_SCENARIO_HPP
#define INSIGHT_METALOG_CORPUS_WINDOWS_SCENARIO_HPP

namespace insight::metalog::corpus_windows
{

// invariant: slack rather than a tuned bound -- the committed corpora are tens of lines.
inline constexpr std::size_t kArenaBytes{std::size_t{1} << 22};

// invariant: literal epoch offsets, so a document from this construction is a function of the
// corpus bytes alone and of nothing on the machine.
inline constexpr std::int64_t kWindowStartEpochSeconds{1'700'000'000};
inline constexpr std::int64_t kWindowMidEpochSeconds{1'700'000'060};
inline constexpr std::int64_t kWindowEndEpochSeconds{1'700'000'120};

inline void configure(insight::metalog::MetaLogConfig& config)
{
    config.max_param_histograms = 3;
    config.emit_stability = true;
}

// post: a trailing CR is dropped, so a CRLF checkout produces the same events as an LF one.
// post: std::nullopt means the file could not be opened; the caller decides what that costs.
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

// invariant: previous is the first half of the tokenized events and current the second, so
// diff(previous, current) and compose(previous, current) are both defined on them.
struct WindowPair
{
    insight::metalog::MetaLogDocument previous;
    insight::metalog::MetaLogDocument current;
};

// post: ingest runs on the CALLER's engine, so the caller keeps the registry to_json needs.
// invariant: the arena is local and MetaLogDocument owns its strings, so the returned pair outlives
// it.
// invariant: the composition is deliberately EMPTY -- this package links no semantic package, and a
// non-empty set would make the document a function of a dialect vocabulary.
// refs: ADR-17.D8
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

#endif
