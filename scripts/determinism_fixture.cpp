// NOLINTBEGIN Test
//  Standing-gate fixture (cross-machine bit-identity proxy).
//
//  Tokenizes a corpus through canon, feeds two windows into MetaLog with
//  histograms + stability, and emits both full JSON documents. Built across the
//  gcc x clang x -O{0,2,3} x -ffp-contract={off,fast} matrix by
//  scripts/determinism_bitidentity.sh, the output must be byte-identical across
//  every build (the local proxy for cross-architecture determinism). The
//  in-suite DeterminismGate golden test pins the same artifact per build; this
//  fixture extends the check across compilers/flags. Timestamps are FIXED so only
//  computed content can differ between builds.
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "insight/metalog/metalog_engine.hpp"
#include "insight/tokenization/arena_allocator.hpp"
#include "insight/tokenization/tokenizer_engine.hpp"

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: determinism_fixture <corpus>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        line.clear();
    }

    namespace tk = insight::tokenization;
    namespace ml = insight::metalog;
    constexpr std::size_t kArenaBytes{std::size_t{1} << 22};
    tk::ArenaAllocator arena{kArenaBytes};
    tk::Tokenizer tok{arena};
    std::vector<tk::CanonicalEvent> events;
    events.reserve(lines.size());
    for (const auto& raw : lines)
        if (auto event{tok.process_line(raw)})
            events.push_back(*event);

    ml::MetaLogConfig cfg;
    cfg.max_param_histograms = 3;
    cfg.emit_stability = true;
    ml::MetaLogEngine engine{cfg};
    using Clock = std::chrono::system_clock;
    const Clock::time_point window_start{std::chrono::seconds{1700000000}};
    const Clock::time_point window_mid{std::chrono::seconds{1700000060}};
    const Clock::time_point window_end{std::chrono::seconds{1700000120}};
    const std::size_t half{events.size() / 2};

    engine.open_window(window_start);
    for (std::size_t i = 0; i < half; ++i)
        engine.ingest_event(events[i]);
    const auto doc1{engine.close_window(window_mid)};
    engine.open_window(window_mid);
    for (std::size_t i = half; i < events.size(); ++i)
        engine.ingest_event(events[i]);
    const auto doc2{engine.close_window(window_end)};

    std::cout << ml::to_json(doc1) << "\n" << ml::to_json(doc2) << "\n";
    return 0;
}

// NOLINTEND Test
