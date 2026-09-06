// note: bare and file-wide; measured to cover 9 diagnostics here, one a WarningsAsErrors class.
// NOLINTBEGIN Test
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <spdlog/common.h>
#include <string>
#include <vector>

#if defined(_WIN32)
// invariant: Windows stdout is text mode by default, so every newline would become CRLF and diverge
// from the LF-only golden -- a harness artifact, never an engine difference.
#include <fcntl.h>
#include <io.h>
#endif

// pre: the canon and metalog module tower is consumed by import; there are no textual public
// headers.
import insight.canon;
import insight.metalog;

// pre: included AFTER the imports, since these are plain textual headers.
// invariant: the same scenarios the in-suite tests run, so both oracles replay identical windows.
#include "collapse_depths_scenario.hpp"
#include "corpus_windows_scenario.hpp"
#include "cube_collapse_scenario.hpp"
#include "latency_shift_scenario.hpp"
#include "ngram_cap_scenario.hpp"
#include "reservoir_nearfull_scenario.hpp"
#include "reservoir_streaming_scenario.hpp"
#include "service_edges_overcap_scenario.hpp"

int main(int argc, char** argv)
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // invariant: NOTHING but the emitted documents may reach this stdout -- an interleaved log
    // record makes the artifact a function of the operator rather than of the corpus.
    // note: the level stays info -- the destination was the defect, not the diagnostics.
    // refs: DN-53.D3
    insight::logging::init_logging(spdlog::level::info);

    if (argc < 2)
    {
        std::cerr << "usage: determinism_fixture <corpus | --reservoir-nearfull | "
                     "--reservoir-streaming | --cube-collapse | --service-edges | "
                     "--ngram-cap | --latency-shift | --collapse-depths>\n";
        return 2;
    }

    // post: emits the collapsed document from the cardinality-explosion window.
    // refs: ADR-31.D8
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

    // post: emits the near-full reservoir document at the batch retention tuple.
    // refs: ADR-31.D8
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

    // post: emits the near-full reservoir document at the shipped streaming tuple.
    // refs: ADR-31.D8
    if (std::string{argv[1]} == "--reservoir-streaming")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::streaming_nearfull::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        engine.open_window(Clock::time_point{std::chrono::seconds{1700000000}});
        ml::streaming_nearfull::emit_window(engine);
        const auto doc{engine.close_window(Clock::time_point{std::chrono::seconds{1700000060}})};
        std::cout << ml::to_json(doc, engine.registry()) << "\n";
        return 0;
    }

    // post: emits the one document in this digest that CARRIES the dropped-observations field.
    // note: every other section stays under the bound, so the field is absent in all of them.
    if (std::string{argv[1]} == "--ngram-cap")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::ngram_cap::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        engine.open_window(Clock::time_point{std::chrono::seconds{1700000000}});
        ml::ngram_cap::emit_window(engine);
        const auto doc{engine.close_window(Clock::time_point{std::chrono::seconds{1700000060}})};
        std::cout << ml::to_json(doc, engine.registry()) << "\n";
        return 0;
    }

    // post: emits the service-edges block decided by the over-cap top-K select.
    // refs: SRC-D-OTEL-21
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

    // post: emits a window PAIR and then the MetaLogDiff between them -- the second artifact
    // species this producer serialises, and the one a change report re-publishes verbatim.
    // note: a diff is where this producer's floating-point lives, the most to lose across corners.
    if (std::string{argv[1]} == "--latency-shift")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::latency_shift::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        const Clock::time_point t0{std::chrono::seconds{1700000000}};
        const Clock::time_point t1{std::chrono::seconds{1700000060}};
        const Clock::time_point t2{std::chrono::seconds{1700000120}};
        engine.open_window(t0);
        ml::latency_shift::emit_window(engine, ml::latency_shift::kPreviousLatencyMs);
        const auto previous{engine.close_window(t1)};
        engine.open_window(t1);
        ml::latency_shift::emit_window(engine, ml::latency_shift::kCurrentLatencyMs);
        const auto current{engine.close_window(t2)};
        // invariant: both inputs THEN the two derived artifacts, so a reader holding only the
        // digest can re-derive the last two records from the first two.
        // invariant: ORDER IS LOAD-BEARING -- the composed record is appended AFTER the diff and
        // the operands are (previous, current), never reversed.
        // note: a digest is a replay artifact, not the place to assert an algebra.
        // refs: DN-82.D2
        std::cout << ml::to_json(previous, engine.registry()) << "\n"
                  << ml::to_json(current, engine.registry()) << "\n"
                  << ml::to_json(ml::diff(previous, current)) << "\n"
                  << ml::to_json(ml::compose(previous, current), engine.registry()) << "\n";
        return 0;
    }

    // post: emits the compare-at-min pair, whose two cubes sit at different collapse depths so the
    // diff is read at the collapsed input's axes.
    // refs: DN-42.D18
    if (std::string{argv[1]} == "--collapse-depths")
    {
        namespace ml = insight::metalog;
        ml::MetaLogConfig cfg;
        ml::collapse_depths::configure(cfg);
        ml::MetaLogEngine engine{cfg};
        using Clock = std::chrono::system_clock;
        const Clock::time_point t0{std::chrono::seconds{1700000000}};
        const Clock::time_point t1{std::chrono::seconds{1700000060}};
        const Clock::time_point t2{std::chrono::seconds{1700000120}};
        engine.open_window(t0);
        ml::collapse_depths::emit_previous(engine);
        const auto previous{engine.close_window(t1)};
        engine.open_window(t1);
        ml::collapse_depths::emit_current(engine);
        const auto current{engine.close_window(t2)};
        // refs: DN-56.D2
        // invariant: composing two cubes at different collapse depths is the one compose clause no
        // corpus pair reaches, since a corpus pair bands both windows alike or neither.
        std::cout << ml::to_json(previous, engine.registry()) << "\n"
                  << ml::to_json(current, engine.registry()) << "\n"
                  << ml::to_json(ml::diff(previous, current)) << "\n"
                  << ml::to_json(ml::compose(previous, current), engine.registry()) << "\n";
        return 0;
    }

    namespace ml = insight::metalog;
    const auto lines{ml::corpus_windows::read_lines(argv[1])};
    if (!lines)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }

    ml::MetaLogConfig cfg;
    ml::corpus_windows::configure(cfg);
    ml::MetaLogEngine engine{cfg};
    const auto pair{ml::corpus_windows::build(engine, *lines)};
    const auto& doc1{pair.previous};
    const auto& doc2{pair.current};

    // invariant: the corpus sections are the only place either operation runs over REAL tokenized
    // log text rather than a synthetic window.
    // invariant: both derived records are emitted after their inputs, so the section stays
    // self-contained.
    std::cout << ml::to_json(doc1, engine.registry()) << "\n"
              << ml::to_json(doc2, engine.registry()) << "\n"
              << ml::to_json(ml::diff(doc1, doc2)) << "\n"
              << ml::to_json(ml::compose(doc1, doc2), engine.registry()) << "\n";
    return 0;
}

// NOLINTEND Test
