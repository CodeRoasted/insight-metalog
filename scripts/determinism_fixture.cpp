// NOLINTBEGIN Test
//  Standing-gate fixture (cross-machine bit-identity proxy).
//
//  Tokenizes a corpus through canon, feeds two windows into MetaLog with
//  histograms + stability, and emits both full JSON documents AND the MetaLogDiff
//  between them (the digest carries BOTH artifact species this producer
//  serializes — see the --latency-shift branch for why that is not optional).
//  Built across the
//  gcc x clang x -O{0,2,3} x -ffp-contract={off,fast} matrix by
//  scripts/determinism_bitidentity.sh, the output must be byte-identical across
//  every build (the local proxy for cross-architecture determinism). The
//  in-suite DeterminismGate golden test pins the same artifact per build; this
//  fixture extends the check across compilers/flags. Timestamps are FIXED so only
//  computed content can differ between builds.
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
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

// AFTER the imports (plain TU): the shared scenarios — the ADR-31.D8 near-full reservoir, the §C3
// cube collapse, the O4b service-edges over-cap topology, and the two window PAIRS whose replayed
// artifact is a MetaLogDiff — shared with the in-suite tests so both oracles run the identical
// windows. `corpus_windows_scenario.hpp` is the odd one out and the reason is worth naming: it is
// not synthetic at all but the tokenize-and-split construction the DEFAULT (corpus-file) branch at
// the bottom of main runs, factored out so the committed golden vectors
// (tests/operations/test_golden_vectors.cpp) pin the artifact this harness emits rather than a
// look-alike built by a second copy of the same twenty-five lines.
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
    _setmode(_fileno(stdout), _O_BINARY); // LF-exact stdout, matching the Linux golden (no CRLF)
#endif

    // Same invariant, second threat. NOTHING but the emitted documents may reach this stdout:
    // canon's engine loggers carry a wall-clock `[%Y-%m-%d %H:%M:%S.%e]` pattern, so an
    // interleaved record makes this artifact a function of the operator rather than of the corpus,
    // and the gate that compares it across the gcc x clang x -O{0,2,3} x -ffp-contract matrix
    // reports a determinism failure that is really a logging failure. Measured here, back when an
    // un-initialised canon resolved to spdlog's default STDOUT logger: two runs of ONE binary on
    // ONE input, two sha256, the differing bytes a wall clock inside a log line.
    // determinism_bitidentity.sh compiled the macros out in its own cells, which is why the matrix
    // legs never saw it — and that is precisely what made the artifact's determinism a property of
    // the CALLER's flags rather than of this fixture.
    //
    // canon's un-initialised state is stderr-only now (DN-53.D3), so this call is no longer what
    // stands between the golden and a log line. It stays for what the quiet fallback deliberately
    // does not give: the module records carry their `[insight.*]` tag, and the level is `info`
    // rather than the fallback's `warn`. Level stays `info` rather than `off`: the diagnostics
    // were never the defect, their DESTINATION was — silencing them would answer artifact purity
    // by deleting observability.
    insight::logging::init_logging(spdlog::level::info);

    if (argc < 2)
    {
        std::cerr << "usage: determinism_fixture <corpus | --reservoir-nearfull | "
                     "--reservoir-streaming | --cube-collapse | --service-edges | "
                     "--ngram-cap | --latency-shift | --collapse-depths>\n";
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

    // The SECOND ADR-31.D8 reservoir oracle, at the tuple the STREAMING surface ships
    // (`salience-1/k128-m64-c0-e16`). The arm above is anchored at the Sift BATCH tuple
    // (top_k 64 / M 128 / reserve 0), so it is silent about both the shipped candidate population
    // and the error-class RESERVE — which is live only here. Replayed across the same
    // gcc×clang × -O{0,3} × -ffp-contract{off,fast} matrix and the same five golden.yaml legs; the
    // emitted document must be byte-identical, or the boundary the deployed configuration decides
    // every window is non-deterministic. Same window as the in-suite ReservoirStreaming guard.
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

    // SPEC §4 n-gram accounting-bound oracle: a SYNTHETIC window whose bigram stream OVERRUNS
    // `max_ngram_keys`, so the emitted document CARRIES `behavior.dropped_ngram_observations`
    // instead of omitting it. Every other section of this digest — the committed corpus and the
    // four synthetic scenarios — stays under the bound, so §4's absence-means-zero encoding makes
    // the field absent in all of them: without this arm the byte-identity sweep and the spec's own
    // §8 validator have never once judged a document that carries the key. The bound BINDS on the
    // real stream (563 of 34 506 GitHub windows at the cut Sift embeds), which is what makes the
    // gap worth a section rather than a note. Same window as the in-suite NgramCapBinds guard.
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

    // The SECOND ARTIFACT SPECIES. Every branch above emits a MetaLogDocument; this one emits a
    // window PAIR and then the `MetaLogDiff` between them — the only other thing this producer
    // serializes, and the one insight-eidos re-publishes verbatim inside every Sift change report
    // (`raw[].diff`). Two properties were unreachable without it. Determinism: a diff is where the
    // producer's floating-point lives (kl/js divergence, per-template frequencies, entropy bits),
    // so it is the artifact with the most to lose across the -ffp-contract{off,fast} corners, and
    // no cell had ever replayed one. Conformance: `metalog_diff.v0.schema.json` governs this
    // species alone, and the digest carried nothing it could be asked about — so the gate over the
    // digest was truthfully green about a subject that excluded half the format
    // (spec_conformance_gate.sh). The pair also drives `cube_diff`'s diff-only differential axis,
    // which is EMERGENT-AT-DIFF and has no stored-cube domain at all: no single-window section can
    // produce one. Same windows as the in-suite LatencyShift guard.
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
        // Both inputs THEN the derived artifact: the section is self-contained, so a reader holding
        // only the digest can re-derive the third line from the first two.
        std::cout << ml::to_json(previous, engine.registry()) << "\n"
                  << ml::to_json(current, engine.registry()) << "\n"
                  << ml::to_json(ml::diff(previous, current)) << "\n";
        return 0;
    }

    // The §16.10 COMPARE-AT-MIN pair (DN-42.D18 property (ii)). Its two cubes sit at different
    // collapse depths — the previous window fires the §C3 banding guardrail, the current window is
    // far under the budget and is never banded — so the diff is read at the coarser of the two and
    // its `cube_diff.axes` equals NEITHER input's `cube.axes`. That is the case §13.6's example
    // text quietly denies and §16.10 mandates, and a conformance corpus made only of same-depth
    // pairs is green and blind on exactly it. Emitting both documents beside the diff is what makes
    // the disagreement checkable inside one section rather than asserted from outside.
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
        std::cout << ml::to_json(previous, engine.registry()) << "\n"
                  << ml::to_json(current, engine.registry()) << "\n"
                  << ml::to_json(ml::diff(previous, current)) << "\n";
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

    // The two documents THEN the diff between them. The corpus sections are the only place a diff
    // is taken over REAL tokenized log text rather than a synthetic window, so they are what makes
    // template_deltas / branching_delta / ngram_delta / tail_delta / reservoir_delta / kl+js
    // divergence — the whole body Sift publishes — a replayed, schema-judged artifact instead of an
    // inferred one. Emitted after its inputs so the section stays self-contained.
    std::cout << ml::to_json(doc1, engine.registry()) << "\n"
              << ml::to_json(doc2, engine.registry()) << "\n"
              << ml::to_json(ml::diff(doc1, doc2)) << "\n";
    return 0;
}

// NOLINTEND Test
