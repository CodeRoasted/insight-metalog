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
#include <spdlog/common.h> // spdlog::level — named for init_logging's sink/level choice in main
#include <string>
#include <vector>

#if defined(_WIN32)
// Windows std::cout is TEXT mode by default → every '\n' becomes CRLF, which would diverge from the
// LF-only Linux golden (a cross-OS HARNESS artifact, not an engine difference — the canon det_proof
// lesson). Put stdout in BINARY mode so the emitted bytes are exactly what the engine wrote.
#include <fcntl.h>
#include <io.h>
#endif

// 1.5.1 unwrap (Approach B): textual public headers are gone — consume the canon+metalog
// module tower. metalog's MetaLog{Config,Engine} via insight.metalog; canon's tokenization
// (ArenaAllocator/Tokenizer/CanonicalEvent) via insight.canon (metalog imports but doesn't
// re-export it).
import insight.canon;
import insight.metalog;

// AFTER the imports (plain TU): the shared synthetic scenarios — the ADR-31.D8 near-full reservoir,
// the §C3 cube collapse, and the O4b service-edges over-cap topology — shared with the in-suite
// tests so both oracles run the identical windows.
#include "cube_collapse_scenario.hpp"
#include "reservoir_nearfull_scenario.hpp"
#include "service_edges_overcap_scenario.hpp"

int main(int argc, char** argv)
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY); // LF-exact stdout, matching the Linux golden (no CRLF)
#endif

    // Same invariant, second threat — and the one canon's det_proof.cpp already fixed for ITS
    // artifact while this sibling fixture kept the defect. NOTHING but the emitted documents may
    // reach this stdout: canon's engine loggers carry a wall-clock `[%Y-%m-%d %H:%M:%S.%e]`
    // pattern, and with no init_logging call they resolve to spdlog's DEFAULT logger, whose sink
    // is stdout. An unelided build therefore interleaves timestamped lines into the bytes this
    // gate compares across the gcc x clang x -O{0,2,3} x -ffp-contract matrix, and the artifact
    // stops being a function of the corpus: two runs of ONE binary on ONE input differ, so the
    // cross-build comparison reports a determinism failure that is really a logging failure.
    // determinism_bitidentity.sh compiles the macros out in its own cells, which is why the
    // matrix legs never saw it — but that makes the artifact's determinism a property of the
    // CALLER's flags rather than of the fixture, and every other caller (a desk run, an
    // inventory build) gets the corrupting default. The sink choice belongs here, where the
    // contract is. Level stays `info` rather than `off`: the diagnostics are not the defect,
    // their DESTINATION was — silencing them would answer artifact purity by deleting
    // observability.
    insight::logging::init_logging(spdlog::level::info, /*diagnostics_to_stderr=*/true);

    if (argc < 2)
    {
        std::cerr
            << "usage: determinism_fixture <corpus | --reservoir-nearfull | --cube-collapse>\n";
        return 2;
    }

    // §C3 cube dimensional-collapse oracle: a SYNTHETIC cardinality-explosion window (not a
    // tokenized corpus) that FIRES the guardrail — the closed cube exceeds the budget and the LEVEL
    // banding {Trace,Debug}→Debug collapses it. Its axis-selection tie-break is an ADR-31.D8-class
    // content decision (a declared total order), so the emitted collapsed document MUST be
    // byte-identical across every leg/arch/OS, or the collapse policy is non-deterministic. Same
    // window as the in-suite CubeCollapse behavioral tests.
    if (std::string{argv[1]} == "--cube-collapse")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::cube_collapse::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        engine.open_window(Clock::time_point{std::chrono::seconds{1700000000}});
        ml::cube_collapse::emit_window(engine);
        const auto doc{engine.close_window(Clock::time_point{std::chrono::seconds{1700000060}})};
        std::cout << ml::to_json(doc, engine.registry()) << "\n";
        return 0;
    }

    // ADR-31.D8 near-full reservoir oracle: a SYNTHETIC scenario (not a tokenized corpus), driven
    // by a flag so the existing corpus files + their golden are untouched.
    // determinism_bitidentity.sh replays it across the gcc×clang × -O{0,3} ×
    // -ffp-contract{off,fast} matrix; the emitted document must be byte-identical across every
    // cell, or the item-reservoir admit/evict boundary is non-deterministic (the ADR-31.D8 leak).
    // Same window as the in-suite ReservoirNearFull golden.
    if (std::string{argv[1]} == "--reservoir-nearfull")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::nearfull::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        engine.open_window(Clock::time_point{std::chrono::seconds{1700000000}});
        ml::nearfull::emit_window(engine);
        const auto doc{engine.close_window(Clock::time_point{std::chrono::seconds{1700000060}})};
        std::cout << ml::to_json(doc, engine.registry()) << "\n";
        return 0;
    }

    // O4b service-topology over-cap oracle (SRC-D-OTEL-21): a SYNTHETIC span window that builds
    // five (caller→callee) edges under a 3-edge cap, so the emitted block is decided by the
    // over-cap top-K select — and the cap cuts THROUGH a 3-way weight tie, so the surviving edge
    // rides the canonical-key tie-break alone. The emitted service_edges block MUST be
    // byte-identical across every leg/arch/OS, or the "deterministic by construction" claim on the
    // top-K select is false (a stdlib-order-dependent select would flip the surviving edge). Same
    // window as the in-suite ServiceEdgesOverCap guard.
    if (std::string{argv[1]} == "--service-edges")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::service_edges_overcap::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        engine.open_window(Clock::time_point{std::chrono::seconds{1700000000}});
        ml::service_edges_overcap::emit_window(engine);
        const auto doc{engine.close_window(Clock::time_point{std::chrono::seconds{1700000060}})};
        std::cout << ml::to_json(doc, engine.registry()) << "\n";
        return 0;
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
    // The degenerate composition (ADR-17): this harness verifies metalog reservoir/cube
    // determinism over SYNTHETIC scenarios (not GHA dialect logs), so it needs no dialect
    // vocabulary — and as a metalog script it does not link the semantic packages. compose({}) is a
    // defined, runnable core-only state.
    const insight::semantic::ComposedSemantics composed{insight::semantic::compose({})};
    tk::Tokenizer tok{arena, tk::MaskConfig{}, composed};
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

    std::cout << ml::to_json(doc1, engine.registry()) << "\n"
              << ml::to_json(doc2, engine.registry()) << "\n";
    return 0;
}

// NOLINTEND Test
