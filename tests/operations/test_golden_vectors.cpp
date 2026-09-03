// Unit tests: allow short identifiers and test-specific patterns.
//
// test_golden_vectors.cpp — the committed GOLDEN VECTORS for `compose()` (SPEC §12) and `diff()`
// (SPEC §13). Until this file, this repo had none: `rg` over `tests/` for a committed expected
// artifact returned the cube-border corpus and nothing else, and every operations suite beside it
// asserts a PROPERTY (§12.2's algebra, §13.2's outcome, §13.6's border recall). A property suite
// says the code is self-consistent. A vector says the code agrees with the DOCUMENT — MetaLog is a
// published, vendor-neutral specification with an owner outside this repo, and only the second of
// those two claims is the product.
//
// ── WHAT A VECTOR PINS: THE WIRE BYTES, WITH NO FREE FIELD AT COMPARE TIME
//
// Each `tests/vectors/<corpus>.vectors.jsonl` holds FOUR records, one per line, byte-for-byte as
// `to_json` emits them:
//
//   line 1  to_json(previous)                      the earlier window
//   line 2  to_json(current)                       the later window
//   line 3  to_json(diff(previous, current))       SPEC §13
//   line 4  to_json(compose(previous, current))    SPEC §12
//
// The two inputs sit beside the two derived artifacts on purpose: a reader holding only the vector
// file can re-derive lines 3 and 4 from lines 1 and 2 with no external oracle. That is the same
// self-containment `scripts/determinism_bitidentity.sh` gives its digest sections, and for the same
// reason — an expectation whose input lives somewhere else is an expectation nobody can check.
//
// The comparison is over the EXACT bytes and declares no free field, which is a deliberate
// departure from the showcase honesty gate next door (`scripts/build_showcase_samples.py`
// `_content_key`: template ids are free there, everything else pinned). The two rulings differ
// because the input does. That gate redacts a runner path INSIDE the log text, so a template id —
// a hash of that text (§3.2) — moves for a reason the claim is not about, and freeing it is the
// only way to keep the rest pinned. Here the input is a log file frozen in git, so the id is a
// deterministic function of committed bytes: pinning it is not churn, it is the ONE thing
// `scripts/spec_conformance_gate.sh` states in as many words that it cannot check —
// *"clause 3 (template_id computed per §3.2) has no pinned cross-implementation vector to check
// against"*. Arm 2 below is that vector.
//
// EXACTLY ONE VALUE IN THE DOCUMENT MOVES FOR A REASON THE CLAIM IS NOT ABOUT: `producer.version`,
// bumped mechanically at every release cut (operations/001 OPS-1.S15). Pinning it would red three
// vector files on a version bump and the repair would be *regenerate the golden* — the reflex that
// destroys goldens, since it is indistinguishable from the repair for a real defect. It is
// therefore removed from the vector's domain AT PRODUCE TIME rather than declared free at compare
// time: the harness stamps `kVectorProducerVersion`, the committed bytes stay exact, and there is
// no free-field policy to maintain or to leak. What that costs is named and paid back below:
// `ProducerVersionIsStampedFromTheOnePackageConstant` witnesses the field the vectors froze.
//
// Everything else is pinned INCLUDING `metalog_version` and `canonicalization_version`, and both
// are load-bearing rather than incidental. A spec bump SHOULD have to re-bless these files — that
// is what makes them a conformance record. And `canonicalization_version` is canon's processing
// contract (§2.4): when it moves, every document here is addressable to a different contract and a
// silent green would be the wrong answer.
//
// ── WHERE THE INPUTS COME FROM: REAL LOG TEXT, AND THE SAME BYTES THE RELEASE GATES REPLAY
//
// Not hand-authored. A vector whose input a test author wrote by hand tests that author's reading
// of the format; these read `scripts/determinism_corpus/*.log` — real committed log lines, already
// the subject of the cross-compiler bit-identity matrix (`determinism_bitidentity.sh`) and of the
// SPEC §8 schema gate (`spec_conformance_gate.sh`) — through the shipped canon tokenizer and
// `MetaLogEngine`. The tokenize-and-split construction is `scripts/corpus_windows_scenario.hpp`,
// shared with `scripts/determinism_fixture.cpp`, so this suite and those two gates judge ONE
// artifact rather than three look-alikes. Three orthogonal claims over the same bytes: the matrix
// says they are STABLE, the schema gate says they are WELL-FORMED, these vectors say they are
// RIGHT.
//
// ── WHY A BYTE COMPARE IS NOT A SELF-REFERENTIAL COMPARE
//
// Arm 1 alone would be: regenerate the file from a broken producer and it goes green. So every
// vector carries a second arm whose ORACLE IS NOT THIS CODE.
//
//   Arm 2 (§3.2) recomputes each `template_id` with **picosha2** — a second SHA-256, not canon's
//          `template_id_of`, which is the implementation under test — and asserts the resulting
//          `"h:" + 32 hex` string is present verbatim in the committed record. picosha2 was already
//          a declared test-only dependency of this package, annotated *"golden doc-digest
//          hashing"*, and had no reader anywhere in the suite; this is the job it was declared for.
//   Arm 3 (§12.1) recomputes compose()'s window arithmetic, its cap minima and its unique-template
//          union FROM THE TWO INPUTS, and asserts §12.1's `stability` MUST-omit.
//   Arm 4 (§13.3) recomputes `delta = current - previous` per template delta and asserts the
//          direction is the one the standard fixes, not the one that happened to be emitted.
//
// A regeneration from a defective producer reds arms 2–4 even when arm 1 is green.
//
// ── AND THE MUTATIONS, BECAUSE A GOLDEN NOBODY HAS SEEN RED IS A GOLDEN NOBODY HAS TESTED
//
// Three per corpus, each pre-registered with its acceptable outcome:
//   M1  drop the corpus's last line          → the four records MUST NOT all match the vector
//   M2  diff(current, previous)              → MUST NOT match line 3   (§13.3 direction is pinned)
//   M3  compose(previous, previous)          → MUST NOT match line 4   (the operands are pinned)
// M2 and M3 are the sharp ones: they mutate nothing about the bytes' SIZE or SHAPE, so a vector
// that passed them by accident would have to be pinning the actual computed content.
//
// Determinism: no RNG, no threads, no wall clock — the window bounds are literal epoch offsets
// owned by the shared construction. Single-threaded by construction.

#include <gtest/gtest.h>

// The umbrella, not <glaze/json/{prettify,minify}.hpp> directly: those two are NOT self-contained
// in glaze 7.4 (they call `read_iterators` without declaring it), so including either alone is a
// compile error. Same include the production serialiser uses (src/serialization/json_egress.hpp).
#include <glaze/glaze.hpp>
#include <picosha2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

import insight.metalog.test;

// AFTER the imports (plain TU): the corpus tokenize-and-split construction shared with
// scripts/determinism_fixture.cpp.
#include "corpus_windows_scenario.hpp"

namespace
{

namespace meta = insight::metalog;
namespace cw = insight::metalog::corpus_windows;

// The frozen stand-in for `producer.version`. Any non-empty string satisfies the schema
// (metalog.v0.schema.json: `version` is `type: string, minLength: 1`); this one is chosen to be
// unmistakably not a release, so a vector line can never be read as evidence about a shipped
// version.
constexpr std::string_view kVectorProducerVersion{"0.0.0-vector"};

// ── The corpora, and why each is in the set ───────────────────────────────────────────────────
//
// Three of the seven committed corpus files, chosen for the SHAPE of the diff they produce rather
// than for size. Adding the other four would quadruple the committed bytes and add no shape: they
// are the same document blocks over different line counts. Each entry's `why` is the reason it
// would be missed if it were dropped.
struct Corpus
{
    std::string_view name;
    std::string_view why;
};

constexpr std::array<Corpus, 3> kCorpora{{
    {"service_a",
     "the RICH shape: 9 -> 12 unique templates across the split, so the diff carries "
     "new_templates, vanished_templates, branching_delta and ngram_delta together, and the "
     "documents carry populated per-slot param_histograms (SPEC 3.5)"},
    {"service_b",
     "the COLLAPSE shape: 6 -> 1 unique templates, a vanish-dominated diff with NO "
     "branching_delta at all — the asymmetric case a rich-only vector set never exercises"},
    {"json_ordinals",
     "the QUIET shape: one template on both sides, so the diff carries neither new nor vanished "
     "templates and reduces to divergence plus template_deltas — the ordinary no-structural-change "
     "output, which is what a consumer sees most often"},
}};

// Without this gtest prints `where GetParam() = 32-byte object <09-00 ...>` on every failure —
// a hex dump of two string_views, which tells a reader nothing. The corpus name is the one thing a
// failure has to carry.
void PrintTo(const Corpus& corpus, std::ostream* os)
{
    *os << corpus.name;
}

// Line indices into a vector file. Named, because `records[2]` in an assertion message tells a
// reader nothing about which artifact failed.
enum Record : std::size_t
{
    kPreviousDocument = 0,
    kCurrentDocument = 1,
    kDiff = 2,
    kComposed = 3,
    kRecordCount = 4
};

[[nodiscard]] std::string_view record_name(std::size_t index)
{
    switch (index)
    {
    case kPreviousDocument:
        return "line 1 — to_json(previous)";
    case kCurrentDocument:
        return "line 2 — to_json(current)";
    case kDiff:
        return "line 3 — to_json(diff(previous, current))  [SPEC 13]";
    case kComposed:
        return "line 4 — to_json(compose(previous, current))  [SPEC 12]";
    default:
        return "<record index out of range>";
    }
}

// ── Producing the four records ────────────────────────────────────────────────────────────────

// One template the vectors reference, carried out of the producing scope because the registry that
// resolves it lives on the engine (SRC-D-TIR-5: the wire is id-only + inline, and the string
// resolves by id from the engine-owned registry).
struct TemplateBinding
{
    std::string rendered_id; // "h:" + 32 lowercase hex, exactly as it appears on the wire
    std::string canonical;   // the canonical (masked) template string SPEC 3.2 hashes
};

struct Produced
{
    meta::MetaLogDocument previous;
    meta::MetaLogDocument current;
    meta::MetaLogDiff diffed;
    meta::MetaLogDocument composed;
    std::vector<std::string> records; // exactly kRecordCount, wire bytes
    std::vector<TemplateBinding> templates;
};

[[nodiscard]] std::filesystem::path corpus_path(std::string_view name)
{
    return std::filesystem::path{INSIGHT_METALOG_CORPUS_DIR} / (std::string{name} + ".log");
}

[[nodiscard]] std::filesystem::path vector_path(std::string_view name)
{
    return std::filesystem::path{INSIGHT_METALOG_VECTOR_DIR} /
           (std::string{name} + ".vectors.jsonl");
}

// Collect every template a document's top_k references, resolved through the registry. Called once
// per document; duplicates across documents are folded by the caller.
void collect_templates(const meta::MetaLogDocument& doc, const meta::TemplateRegistry& registry,
                       std::vector<TemplateBinding>& out)
{
    for (const auto& entry : doc.stats.top_k)
    {
        auto rendered{insight::render(entry.template_id)};
        const bool already{std::ranges::any_of(out, [&rendered](const TemplateBinding& binding)
                                               { return binding.rendered_id == rendered; })};
        if (already)
            continue;
        if (!registry.contains(entry.template_id))
            continue; // an id with no interned string cannot be checked against 3.2 here
        out.push_back({.rendered_id = std::move(rendered),
                       .canonical = std::string{registry.lookup(entry.template_id)}});
    }
}

// `lines` is passed in rather than read here, so the mutation arms can hand in a perturbed corpus
// through the identical path the unperturbed one takes.
[[nodiscard]] Produced produce_from(const std::vector<std::string>& lines)
{
    meta::MetaLogConfig config;
    cw::configure(config);
    config.producer_version = std::string{kVectorProducerVersion};

    meta::MetaLogEngine engine{config};
    auto pair{cw::build(engine, lines)};

    Produced produced;
    produced.previous = std::move(pair.previous);
    produced.current = std::move(pair.current);
    produced.diffed = meta::diff(produced.previous, produced.current);
    produced.composed = meta::compose(produced.previous, produced.current);

    produced.records.resize(kRecordCount);
    produced.records[kPreviousDocument] = meta::to_json(produced.previous, engine.registry());
    produced.records[kCurrentDocument] = meta::to_json(produced.current, engine.registry());
    produced.records[kDiff] = meta::to_json(produced.diffed);
    produced.records[kComposed] = meta::to_json(produced.composed, engine.registry());

    collect_templates(produced.previous, engine.registry(), produced.templates);
    collect_templates(produced.current, engine.registry(), produced.templates);
    collect_templates(produced.composed, engine.registry(), produced.templates);
    return produced;
}

// ── Failure reporting ─────────────────────────────────────────────────────────────────────────
//
// A wire record is one JSON line of up to ~10 KB. `EXPECT_EQ` over two such strings prints both in
// full and leaves the reader to find the difference by eye, which is not a diagnosis. So the
// report prettifies both sides — a purely TEXTUAL reformat, asserted lossless by
// `PrettifiedDiffReportingIsLossless` below — and names the differing LINES.

[[nodiscard]] std::vector<std::string> split_lines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t begin{0};
    while (begin <= text.size())
    {
        const auto end{text.find('\n', begin)};
        if (end == std::string_view::npos)
        {
            lines.emplace_back(text.substr(begin));
            break;
        }
        lines.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

constexpr std::size_t kMaxReportedLines{8};

[[nodiscard]] std::string describe_difference(const std::string& expected,
                                              const std::string& actual)
{
    std::string report;
    report += "\n  expected " + std::to_string(expected.size()) + " byte(s), actual " +
              std::to_string(actual.size()) + " byte(s)";

    const auto common{std::ranges::mismatch(expected, actual)};
    const auto offset{static_cast<std::size_t>(common.in1 - expected.begin())};
    report += "; first differing byte at offset " + std::to_string(offset);

    const auto expected_lines{split_lines(glz::prettify_json(expected))};
    const auto actual_lines{split_lines(glz::prettify_json(actual))};
    report += "\n  prettified: expected " + std::to_string(expected_lines.size()) +
              " line(s), actual " + std::to_string(actual_lines.size()) + " line(s)";

    std::size_t reported{0};
    std::size_t differing{0};
    const auto limit{std::max(expected_lines.size(), actual_lines.size())};
    for (std::size_t i = 0; i < limit; ++i)
    {
        const std::string_view left{i < expected_lines.size() ? std::string_view{expected_lines[i]}
                                                              : std::string_view{"<absent>"}};
        const std::string_view right{i < actual_lines.size() ? std::string_view{actual_lines[i]}
                                                             : std::string_view{"<absent>"}};
        if (left == right)
            continue;
        ++differing;
        if (reported >= kMaxReportedLines)
            continue;
        ++reported;
        report += "\n    line " + std::to_string(i + 1) + "\n      golden: " + std::string{left} +
                  "\n      actual: " + std::string{right};
    }
    if (differing > reported)
        report += "\n    ... and " + std::to_string(differing - reported) + " further line(s)";
    if (differing == 0)
        report += "\n    (no prettified line differs — the difference is in whitespace or in a "
                  "byte the reformatter normalises; compare the raw records)";
    return report;
}

// ── The reader for a committed vector file, and the red-only actual dump ──────────────────────
//
// On a RED — an absent golden, a wrong record count, or any record that differs — the four records
// this build produced are written beside the golden as `<name>.vectors.jsonl.actual` and the path
// is named in the failure. That is a diagnostic, not a regeneration affordance, and the boundary is
// the whole point: it NEVER writes the golden itself, it fires only when an assertion has already
// failed, and the reviewer's next step is `diff` between two files rather than a 10 KB gtest
// message. Repairing a red by copying the dump over the golden is still a deliberate act by a
// human who has read the diff — which is the act that must stay deliberate.
// `.gitignore` carries `tests/vectors/*.actual` so a dump cannot be committed by accident.

[[nodiscard]] std::optional<std::vector<std::string>> read_vector_file(std::string_view name)
{
    std::ifstream input(vector_path(name), std::ios::binary);
    if (!input)
        return std::nullopt;
    std::vector<std::string> records;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        records.push_back(std::move(line));
        line.clear();
    }
    return records;
}

// Returns the path written, or a bracketed reason it could not be — the return value is streamed
// straight into a gtest failure, and a dump that silently did not happen would leave the reader
// chasing a file that is not there.
[[nodiscard]] std::string dump_actual(std::string_view name,
                                      const std::vector<std::string>& records)
{
    const auto path{vector_path(name).string() + ".actual"};
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return "<could not open " + path + " for writing>";
    for (const auto& record : records)
        out << record << "\n";
    out.flush();
    if (!out)
        return "<write failed for " + path + ">";
    return path;
}

// ── The suite ─────────────────────────────────────────────────────────────────────────────────

class GoldenVector : public ::testing::TestWithParam<Corpus>
{
};

// ARM 1 — the vector itself. The four records this producer emits today MUST be byte-identical to
// the four committed for this corpus.
TEST_P(GoldenVector, ReproducesTheCommittedVector)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string()
        << "\n  the vector's INPUT is not optional — without it this test would pass by not "
           "looking";
    const auto produced{produce_from(*lines)};
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value())
        << "golden vector file is missing: " << vector_path(corpus.name).string()
        << "\n  what this build produces is dumped at: "
        << dump_actual(corpus.name, produced.records)
        << "\n  READ IT before adopting it: a golden adopted from a run is a record of that run, "
           "not of the standard. The 3.2 / 12.1 / 13.3 arms in this file are what decide whether "
           "these bytes are RIGHT.";
    if (golden->size() != static_cast<std::size_t>(kRecordCount))
        ADD_FAILURE() << "golden " << vector_path(corpus.name).string() << " holds "
                      << golden->size() << " record(s); a vector file is exactly " << kRecordCount
                      << " (previous, current, diff, composed)"
                      << "\n  what this build produces is dumped at: "
                      << dump_actual(corpus.name, produced.records);
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    bool matched{true};
    for (std::size_t i = 0; i < kRecordCount; ++i)
    {
        if ((*golden)[i] == produced.records[i])
            continue;
        matched = false;
        ADD_FAILURE() << "[" << corpus.name << "] " << record_name(i)
                      << " does not match its golden vector."
                      << "\n  why this corpus is in the set: " << corpus.why
                      << describe_difference((*golden)[i], produced.records[i]);
    }
    if (!matched)
        GTEST_LOG_(INFO) << "[" << corpus.name << "] full produced artifact dumped at: "
                         << dump_actual(corpus.name, produced.records) << " — diff it against "
                         << vector_path(corpus.name).string();
}

// ARM 2 — SPEC 3.2, against an oracle that is NOT this code. `template_id = "h:" +
// lower_hex(SHA-256(template_string)[0:16])`, recomputed with picosha2 and asserted to appear
// verbatim in the committed bytes. The second half of that sentence is what ties the derivation to
// the FILE: a green here says the golden carries an id that a second SHA-256 agrees with, not
// merely that two calls into canon agreed with each other.
TEST_P(GoldenVector, TemplateIdsInTheVectorAreSha256OfTheirTemplate)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value())
        << "golden vector file is missing: " << vector_path(corpus.name).string();
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    const auto produced{produce_from(*lines)};
    ASSERT_FALSE(produced.templates.empty())
        << "[" << corpus.name
        << "] no top_k template resolved through the registry, so this arm checked NOTHING. A "
           "vector set whose 3.2 arm has no subject is the vacuous green this arm exists to "
           "prevent.";

    // 32 bytes -> 64 hex; SPEC 3.2 truncates the digest to its first 16 bytes = 32 hex.
    constexpr std::size_t kIdHexChars{32};
    for (const auto& binding : produced.templates)
    {
        const std::string full_digest{picosha2::hash256_hex_string(binding.canonical)};
        const std::string expected_id{"h:" + full_digest.substr(0, kIdHexChars)};
        EXPECT_EQ(expected_id, binding.rendered_id)
            << "[" << corpus.name << "] SPEC 3.2 violated for template \"" << binding.canonical
            << "\"\n  independent SHA-256 (picosha2), full digest: " << full_digest
            << "\n  expected id (h: + first " << kIdHexChars << " hex): " << expected_id
            << "\n  producer emitted:                              " << binding.rendered_id
            << "\n  Two MetaLogs from different producers describing the same template MUST "
               "compute the same template_id; that is what makes MetaLogs comparable across "
               "implementations.";

        const bool in_previous{(*golden)[kPreviousDocument].find(binding.rendered_id) !=
                               std::string::npos};
        const bool in_current{(*golden)[kCurrentDocument].find(binding.rendered_id) !=
                              std::string::npos};
        const bool in_composed{(*golden)[kComposed].find(binding.rendered_id) != std::string::npos};
        EXPECT_TRUE(in_previous || in_current || in_composed)
            << "[" << corpus.name << "] id " << binding.rendered_id << " (template \""
            << binding.canonical
            << "\") was resolved from the live registry but appears in NO committed record, so the "
               "3.2 derivation just checked says nothing about the golden file.";
    }
}

// ARM 3 — SPEC 12.1, recomputed from the two inputs. Every clause here is a sentence of the
// standard evaluated over A and B, never a second call into compose().
TEST_P(GoldenVector, ComposedRecordObeysSection12Arithmetic)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    const auto produced{produce_from(*lines)};

    const auto& a{produced.previous};
    const auto& b{produced.current};
    const auto& c{produced.composed};

    // "C.window.lines_observed = A.window.lines_observed + B.window.lines_observed", and 12.3's
    // stronger restatement: no lines are invented or lost.
    EXPECT_EQ(c.window.lines_observed, a.window.lines_observed + b.window.lines_observed)
        << "[" << corpus.name << "] SPEC 12.1/12.3: composed lines_observed "
        << c.window.lines_observed << " != " << a.window.lines_observed << " + "
        << b.window.lines_observed << " = " << (a.window.lines_observed + b.window.lines_observed);

    // "C.window.start = min(...)" / "C.window.end = max(...)". The bounds are fixed-width RFC 3339
    // UTC strings ("2023-11-14T22:13:20Z"), so lexicographic order IS chronological order; that is
    // a property of the emitted spelling, not an assumption about strings in general.
    EXPECT_EQ(c.window.start_iso, std::min(a.window.start_iso, b.window.start_iso))
        << "[" << corpus.name << "] SPEC 12.1: composed window.start " << c.window.start_iso
        << " != min(" << a.window.start_iso << ", " << b.window.start_iso << ")";
    EXPECT_EQ(c.window.end_iso, std::max(a.window.end_iso, b.window.end_iso))
        << "[" << corpus.name << "] SPEC 12.1: composed window.end " << c.window.end_iso
        << " != max(" << a.window.end_iso << ", " << b.window.end_iso << ")";

    // "C.window.duration_seconds = C.window.end - C.window.start (real time, NOT the sum of the
    // inputs)". Derived from the frozen window axis rather than from either input's own duration,
    // which is precisely the value the clause warns against summing.
    constexpr std::uint64_t kSpannedSeconds{
        static_cast<std::uint64_t>(cw::kWindowEndEpochSeconds - cw::kWindowStartEpochSeconds)};
    EXPECT_EQ(c.window.duration_seconds, kSpannedSeconds)
        << "[" << corpus.name << "] SPEC 12.1: composed duration_seconds "
        << c.window.duration_seconds << " != the real-time span " << kSpannedSeconds
        << "s; the inputs' own durations are " << a.window.duration_seconds << "s and "
        << b.window.duration_seconds
        << "s, and summing them (= " << (a.window.duration_seconds + b.window.duration_seconds)
        << "s) is the mistake the clause names.";

    // "C.stability MUST be omitted (it is meaningless across composed inputs)". The current window
    // carries one, so this is a real removal and not a vacuous absence — asserted, so the arm goes
    // red rather than quiet if the corpus ever stops producing one.
    ASSERT_TRUE(b.stability.has_value())
        << "[" << corpus.name
        << "] the later window carries no stability block, so the MUST-omit clause below would be "
           "vacuously satisfied and this arm would say nothing.";
    EXPECT_FALSE(c.stability.has_value())
        << "[" << corpus.name
        << "] SPEC 12.1: a composed document MUST omit `stability`; one was emitted.";

    // DN-56.D2 / SPEC 12.1: a composed document declares its own caps as the MINIMUM over the caps
    // the inputs actually declared. Both inputs here come from one config, so the minimum is that
    // config's value — which is exactly why an inequality would be a real defect and not a
    // scope-dependence artifact.
    EXPECT_EQ(c.stats.top_k_size, std::min(a.stats.top_k_size, b.stats.top_k_size))
        << "[" << corpus.name << "] SPEC 12.1: composed top_k_size " << c.stats.top_k_size
        << " != min(" << a.stats.top_k_size << ", " << b.stats.top_k_size << ")";

    // SPEC 8 clause 4: a DECLARED cap bounds its array.
    EXPECT_LE(c.stats.top_k.size(), c.stats.top_k_size)
        << "[" << corpus.name << "] SPEC 8 clause 4: composed top_k holds " << c.stats.top_k.size()
        << " entr(ies) against a declared top_k_size of " << c.stats.top_k_size;

    // "C.stats.unique_templates is recomputed from the union". The union is only computable here
    // because BOTH inputs have an empty tail — with a non-empty tail the per-template counts of the
    // tail members are unknown (12.3) and the union is not derivable from the documents. The
    // precondition is ASSERTED, so a corpus that grows a tail reds this arm instead of silently
    // weakening it.
    ASSERT_EQ(a.stats.tail_unique, 0U)
        << "[" << corpus.name
        << "] the earlier window grew a tail (tail_unique=" << a.stats.tail_unique
        << "), so the unique-template union below is no longer derivable from the documents "
           "(SPEC 12.3). Re-derive the expectation or drop this corpus from the set.";
    ASSERT_EQ(b.stats.tail_unique, 0U)
        << "[" << corpus.name
        << "] the later window grew a tail (tail_unique=" << b.stats.tail_unique
        << "), so the unique-template union below is no longer derivable (SPEC 12.3).";
    std::vector<insight::TemplateId> union_ids;
    for (const auto* doc : {&a, &b})
        for (const auto& entry : doc->stats.top_k)
            if (!std::ranges::any_of(union_ids, [&entry](const insight::TemplateId& id)
                                     { return id == entry.template_id; }))
                union_ids.push_back(entry.template_id);
    EXPECT_EQ(c.stats.unique_templates, static_cast<std::uint64_t>(union_ids.size()))
        << "[" << corpus.name << "] SPEC 12.1: composed unique_templates "
        << c.stats.unique_templates
        << " != |ids(A.top_k) union ids(B.top_k)| = " << union_ids.size() << " (A declares "
        << a.stats.unique_templates << ", B declares " << b.stats.unique_templates
        << ", both tails empty)";
}

// ARM 4 — SPEC 13.3. "previous is the earlier document; current is the later document;
// delta = current - previous. Positive = grew; negative = shrank."
TEST_P(GoldenVector, DiffRecordObeysSection133DirectionAndSign)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    const auto produced{produce_from(*lines)};

    ASSERT_FALSE(produced.diffed.template_deltas.empty())
        << "[" << corpus.name
        << "] the diff carries no template_deltas, so the sign clause below has no subject.";

    for (const auto& delta : produced.diffed.template_deltas)
    {
        const auto expected{static_cast<std::int64_t>(delta.current_count) -
                            static_cast<std::int64_t>(delta.previous_count)};
        EXPECT_EQ(delta.delta, expected)
            << "[" << corpus.name << "] SPEC 13.3 violated for "
            << insight::render(delta.template_id) << ": delta=" << delta.delta
            << " but current_count(" << delta.current_count << ") - previous_count("
            << delta.previous_count << ") = " << expected;
    }

    // The counts themselves must be the two windows' own counts, in that order — otherwise the
    // clause above holds over a pair of numbers that are not the documents'.
    const auto count_in{[](const meta::MetaLogDocument& doc, const insight::TemplateId& id)
                        {
                            for (const auto& entry : doc.stats.top_k)
                                if (entry.template_id == id)
                                    return entry.count;
                            return std::uint64_t{0};
                        }};
    for (const auto& delta : produced.diffed.template_deltas)
    {
        EXPECT_EQ(delta.previous_count, count_in(produced.previous, delta.template_id))
            << "[" << corpus.name << "] SPEC 13.3: `previous` is the EARLIER document, but "
            << insight::render(delta.template_id)
            << " reports previous_count=" << delta.previous_count
            << " while the earlier window's top_k holds "
            << count_in(produced.previous, delta.template_id);
        EXPECT_EQ(delta.current_count, count_in(produced.current, delta.template_id))
            << "[" << corpus.name << "] SPEC 13.3: `current` is the LATER document, but "
            << insight::render(delta.template_id)
            << " reports current_count=" << delta.current_count
            << " while the later window's top_k holds "
            << count_in(produced.current, delta.template_id);
    }
}

// MUTATION M1 — the input. Drop the corpus's last line and the four records MUST NOT all still
// match. A vector that survives a change to its own input is pinning nothing.
TEST_P(GoldenVector, MutatedCorpusDoesNotReproduceTheVector)
{
    const auto& corpus{GetParam()};
    auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    ASSERT_GT(lines->size(), 1U) << "[" << corpus.name
                                 << "] a one-line corpus cannot carry this mutation";
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value());
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    lines->pop_back();
    const auto mutated{produce_from(*lines)};

    std::size_t survived{0};
    std::string survivor_names;
    std::string moved_names;
    for (std::size_t i = 0; i < kRecordCount; ++i)
    {
        if ((*golden)[i] == mutated.records[i])
        {
            ++survived;
            survivor_names += "\n    " + std::string{record_name(i)};
        }
        else
        {
            moved_names += "\n    " + std::string{record_name(i)};
        }
    }
    // The pre-registered acceptable outcome: STRICTLY fewer than all four records survive.
    EXPECT_LT(survived, static_cast<std::size_t>(kRecordCount))
        << "[" << corpus.name
        << "] dropping the corpus's LAST line left EVERY record byte-identical to the golden, so "
           "the vector is insensitive to its own input and pins nothing. Either the corpus's last "
           "line does not reach the engine (an unparsed line is silently dropped by the tokenizer) "
           "or the records are not a function of the input at all.";
    // Reported, never asserted: WHICH records a one-line drop leaves alone depends on where the
    // midpoint split lands, and pinning that would pin the mutation instead of the artifact.
    GTEST_LOG_(INFO) << "[" << corpus.name << "] last-line drop moved " << (kRecordCount - survived)
                     << " of " << kRecordCount << " record(s)."
                     << (moved_names.empty() ? "" : "\n  moved:") << moved_names
                     << (survivor_names.empty() ? "" : "\n  unchanged:") << survivor_names;
}

// MUTATION M2 — SPEC 13.3's direction. Swapping the operands MUST NOT reproduce line 3.
TEST_P(GoldenVector, ReversedDiffDoesNotReproduceTheVector)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value());
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    const auto produced{produce_from(*lines)};
    const auto reversed{meta::to_json(meta::diff(produced.current, produced.previous))};
    EXPECT_NE((*golden)[kDiff], reversed)
        << "[" << corpus.name
        << "] diff(current, previous) serialises to the SAME bytes as the golden "
           "diff(previous, current). SPEC 13.3 fixes the roles of the two inputs, so a vector that "
           "cannot tell the two apart pins no direction at all.";
}

// MUTATION M3 — the compose operands. Composing a document with ITSELF MUST NOT reproduce line 4.
TEST_P(GoldenVector, SelfComposeDoesNotReproduceTheVector)
{
    const auto& corpus{GetParam()};
    const auto lines{cw::read_lines(corpus_path(corpus.name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(corpus.name).string();
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value());
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    meta::MetaLogConfig config;
    cw::configure(config);
    config.producer_version = std::string{kVectorProducerVersion};
    meta::MetaLogEngine engine{config};
    const auto pair{cw::build(engine, *lines)};
    const auto self_composed{
        meta::to_json(meta::compose(pair.previous, pair.previous), engine.registry())};
    EXPECT_NE((*golden)[kComposed], self_composed)
        << "[" << corpus.name
        << "] compose(previous, previous) serialises to the SAME bytes as the golden "
           "compose(previous, current). The vector does not pin which documents were merged.";
}

// The licence for the failure reporter above: prettify is a purely textual reformat, so the lines
// it prints are the record's own bytes rearranged and not a re-encoding. Asserted rather than
// assumed — a prettifier that normalised a number token would make every diff report a plausible
// lie about the wire.
TEST_P(GoldenVector, PrettifiedDiffReportingIsLossless)
{
    const auto& corpus{GetParam()};
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value())
        << "golden vector file is missing: " << vector_path(corpus.name).string();
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    for (std::size_t i = 0; i < kRecordCount; ++i)
    {
        // `glz::minify_json` takes a resizable lvalue (it pads the buffer in place and restores
        // it), so the prettified form is materialised into a named string first.
        std::string prettified{glz::prettify_json((*golden)[i])};
        const auto round_tripped{glz::minify_json(prettified)};
        EXPECT_EQ((*golden)[i], round_tripped)
            << "[" << corpus.name << "] " << record_name(i)
            << ": prettify -> minify is not the identity on this record, so the line diff printed "
               "on a vector mismatch would not be a diff of the real wire bytes."
            << "\n  original     " << (*golden)[i].size() << " byte(s)"
            << "\n  round-tripped " << round_tripped.size() << " byte(s)";
    }
}

INSTANTIATE_TEST_SUITE_P(Corpora, GoldenVector, ::testing::ValuesIn(kCorpora),
                         [](const ::testing::TestParamInfo<Corpus>& info)
                         { return std::string{info.param.name}; });

// The one value the vectors froze out of their domain, witnessed here so the freeze costs no
// coverage: with the DEFAULT config the engine stamps this package's single version constant into
// `producer.version` (SPEC 2.1). kProducerVersion is hand-carried and its bump rides the cut
// ceremony, so this arm is what fails if the two spellings ever diverge again.
TEST(GoldenVectorProducerEnvelope, ProducerVersionIsStampedFromTheOnePackageConstant)
{
    const auto lines{cw::read_lines(corpus_path(kCorpora.front().name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(kCorpora.front().name).string();

    meta::MetaLogConfig config; // DEFAULT producer_version — the point of this arm
    cw::configure(config);
    meta::MetaLogEngine engine{config};
    const auto pair{cw::build(engine, *lines)};

    EXPECT_EQ(pair.previous.producer.version, std::string{meta::kProducerVersion})
        << "the engine stamped producer.version=\"" << pair.previous.producer.version
        << "\" while the package constant kProducerVersion is \"" << meta::kProducerVersion
        << "\"; the two spellings were independent literals once and sat four minor versions "
           "apart.";
    EXPECT_NE(pair.previous.producer.version, std::string{kVectorProducerVersion})
        << "the default producer version equals the vectors' frozen sentinel \""
        << kVectorProducerVersion
        << "\", which would make the vectors silently pin the real version after all.";
    EXPECT_EQ(pair.previous.metalog_version, std::string{meta::kMetaLogSpecVersion})
        << "the document declares metalog_version=\"" << pair.previous.metalog_version
        << "\" while this build's kMetaLogSpecVersion is \"" << meta::kMetaLogSpecVersion << "\".";
}

} // namespace
