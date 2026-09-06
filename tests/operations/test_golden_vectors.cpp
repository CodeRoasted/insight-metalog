
// refs: F-SRC-metalog-spec:SPEC.md, OPS-1.S15
// invariant: these are the committed GOLDEN VECTORS for compose() and diff(); every other suite
// here asserts a PROPERTY, and a property says only that the code is self-consistent.
// invariant: a vector says the code agrees with the published DOCUMENT, and only that is product.
// invariant: each vectors.jsonl holds FOUR records byte-for-byte as to_json emits them: the two
// input windows, then diff(previous, current), then compose(previous, current).
// note: the inputs sit beside the derived artifacts so a reader can re-derive 3 and 4 from 1 and 2.
// invariant: the compare is over EXACT bytes and declares NO free field, unlike the showcase
// honesty gate next door.
// invariant: the two rulings differ because the input does -- here the input is a log frozen in
// git, so a template id is a deterministic function of committed bytes.
// invariant: exactly one value is removed from the vector's domain AT PRODUCE TIME rather than
// freed at compare time: producer.version, which the cut bumps mechanically.
// invariant: the harness stamps a frozen stand-in, so the committed bytes stay exact and there is
// no free-field policy to maintain or to leak.
// invariant: metalog_version and canonicalization_version stay pinned on purpose, because a spec
// bump SHOULD have to re-bless these files.
// invariant: a moved canon contract makes every document here addressable to a different contract,
// so a silent green would be the wrong answer.
// invariant: the inputs are real committed log text read through the shipped tokenizer, never hand
// authored, and the construction is shared with the cross-leg fixture.
// note: three gates judge ONE artifact: STABLE, WELL-FORMED, and RIGHT.
// invariant: arm 1 alone would go green on a regenerated file from a broken producer, so every
// vector carries a second arm whose ORACLE IS NOT THIS CODE.
// note: arm 2 recomputes each template_id with picosha2, a second SHA-256 and not the code here.
// invariant: arm 3 recomputes compose()'s window arithmetic, cap minima and template union from the
// two inputs; arm 4 recomputes the diff delta direction the standard fixes.
// invariant: three mutations per corpus, each pre-registered: drop the corpus's last line, swap the
// diff operands, compose a document with itself -- none may still match the vector.
// note: M2 and M3 change neither size nor shape, so passing them by accident is not available.
#include <gtest/gtest.h>

// note: the umbrella header, because prettify and minify are not self-contained in glaze 7.4.
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

// note: after the imports, plain TU: the construction shared with the cross-leg fixture.
#include "corpus_windows_scenario.hpp"

namespace
{

namespace meta = insight::metalog;
namespace cw = insight::metalog::corpus_windows;

// invariant: the frozen stand-in for producer.version, chosen to be unmistakably not a release so a
// vector line can never be read as evidence about a shipped version.
constexpr std::string_view kVectorProducerVersion{"0.0.0-vector"};

// invariant: three of the seven committed corpora, chosen for the SHAPE of the diff they produce
// rather than for size.
// note: the other four differ in input FORMAT, which the determinism and schema gates cover.
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

// note: without this, gtest prints a hex dump of two string_views on every failure.
void PrintTo(const Corpus& corpus, std::ostream* os)
{
    *os << corpus.name;
}

// note: named line indices, because records[2] in a message names no artifact.
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

// refs: SRC-D-TIR-5
// invariant: the wire is id-only plus inline, so a template string resolves by id from the
// engine-owned registry and must be carried out of the producing scope.
struct TemplateBinding
{
    std::string rendered_id;
    std::string canonical;
};

struct Produced
{
    meta::MetaLogDocument previous;
    meta::MetaLogDocument current;
    meta::MetaLogDiff diffed;
    meta::MetaLogDocument composed;
    std::vector<std::string> records;
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

// note: called once per document; duplicates across documents are folded by the caller.
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
            continue;
        out.push_back({.rendered_id = std::move(rendered),
                       .canonical = std::string{registry.lookup(entry.template_id)}});
    }
}

// invariant: lines are passed in rather than read here, so a mutation arm hands a perturbed corpus
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

// invariant: a wire record is one JSON line of up to about 10 KB, so the report prettifies both
// sides -- a purely TEXTUAL reformat, asserted lossless below -- and names the differing LINES.
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

// invariant: on a RED the four records this build produced are written beside the golden as a
// .actual file and the path is named in the failure.
// invariant: that is a diagnostic and never a regeneration affordance -- it never writes the
// golden, it fires only after an assertion has failed, and .gitignore keeps the dump uncommittable.
// note: repairing a red by copying the dump over the golden stays a deliberate human act.
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

// invariant: returns the path written or a bracketed reason it could not be, because the value is
// streamed into a failure and a silent no-op would send the reader after a file that is not there.
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

class GoldenVector : public ::testing::TestWithParam<Corpus>
{
};

// invariant: arm 1 -- the four records this producer emits today must be byte-identical to the four
// committed for this corpus.
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

// refs: F-SRC-metalog-spec:SPEC.md
// invariant: arm 2 -- the template id is recomputed with picosha2 and asserted to appear VERBATIM
// in the committed bytes, so a green says the golden carries an id a second SHA-256 agrees with.
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

    // note: 32 bytes gives 64 hex; the spec truncates the digest to its first 16 bytes, so 32 hex.
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

// invariant: arm 3 -- every clause here is a sentence of the standard evaluated over A and B, never
// a second call into compose().
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

    // invariant: composed lines_observed is the sum of the inputs': no lines are invented or lost.
    EXPECT_EQ(c.window.lines_observed, a.window.lines_observed + b.window.lines_observed)
        << "[" << corpus.name << "] SPEC 12.1/12.3: composed lines_observed "
        << c.window.lines_observed << " != " << a.window.lines_observed << " + "
        << b.window.lines_observed << " = " << (a.window.lines_observed + b.window.lines_observed);

    // invariant: the window bounds are fixed-width RFC 3339 UTC strings, so lexicographic order IS
    // chronological order -- a property of the emitted spelling, not of strings in general.
    EXPECT_EQ(c.window.start_iso, std::min(a.window.start_iso, b.window.start_iso))
        << "[" << corpus.name << "] SPEC 12.1: composed window.start " << c.window.start_iso
        << " != min(" << a.window.start_iso << ", " << b.window.start_iso << ")";
    EXPECT_EQ(c.window.end_iso, std::max(a.window.end_iso, b.window.end_iso))
        << "[" << corpus.name << "] SPEC 12.1: composed window.end " << c.window.end_iso
        << " != max(" << a.window.end_iso << ", " << b.window.end_iso << ")";

    // invariant: the composed duration is derived from the frozen window axis and NOT summed from
    // the inputs' own durations, which is the value the clause warns against summing.
    constexpr std::uint64_t kSpannedSeconds{
        static_cast<std::uint64_t>(cw::kWindowEndEpochSeconds - cw::kWindowStartEpochSeconds)};
    EXPECT_EQ(c.window.duration_seconds, kSpannedSeconds)
        << "[" << corpus.name << "] SPEC 12.1: composed duration_seconds "
        << c.window.duration_seconds << " != the real-time span " << kSpannedSeconds
        << "s; the inputs' own durations are " << a.window.duration_seconds << "s and "
        << b.window.duration_seconds
        << "s, and summing them (= " << (a.window.duration_seconds + b.window.duration_seconds)
        << "s) is the mistake the clause names.";

    // invariant: composed stability MUST be omitted, and the current window carries one, so this is
    // a real removal and goes red rather than quiet if the corpus stops producing one.
    ASSERT_TRUE(b.stability.has_value())
        << "[" << corpus.name
        << "] the later window carries no stability block, so the MUST-omit clause below would be "
           "vacuously satisfied and this arm would say nothing.";
    EXPECT_FALSE(c.stability.has_value())
        << "[" << corpus.name
        << "] SPEC 12.1: a composed document MUST omit `stability`; one was emitted.";

    // refs: DN-56.D2
    // invariant: a composed document declares its caps as the MINIMUM over the caps the inputs
    // declared, and both inputs share one config, so an inequality is a defect.
    EXPECT_EQ(c.stats.top_k_size, std::min(a.stats.top_k_size, b.stats.top_k_size))
        << "[" << corpus.name << "] SPEC 12.1: composed top_k_size " << c.stats.top_k_size
        << " != min(" << a.stats.top_k_size << ", " << b.stats.top_k_size << ")";

    // note: a DECLARED cap bounds its array.
    EXPECT_LE(c.stats.top_k.size(), c.stats.top_k_size)
        << "[" << corpus.name << "] SPEC 8 clause 4: composed top_k holds " << c.stats.top_k.size()
        << " entr(ies) against a declared top_k_size of " << c.stats.top_k_size;

    // invariant: the union is computable only because BOTH inputs have an empty tail, a tailed
    // input's per-template counts being unknown.
    // invariant: that precondition is ASSERTED, so a corpus growing a tail reds this arm rather
    // than silently weakening it.
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

// invariant: arm 4 -- previous is the earlier document and current the later, so the delta is
// current minus previous, positive meaning grew.
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

    // invariant: the counts must be the two windows' own counts in that order, or the clause holds
    // over a pair of numbers that are not the documents'.
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

// invariant: mutation M1 -- drop the corpus's last line and the four records must not all still
// match, because a vector that survives a change to its own input is pinning nothing.
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
    // note: the pre-registered outcome: strictly fewer than all four records survive.
    EXPECT_LT(survived, static_cast<std::size_t>(kRecordCount))
        << "[" << corpus.name
        << "] dropping the corpus's LAST line left EVERY record byte-identical to the golden, so "
           "the vector is insensitive to its own input and pins nothing. Either the corpus's last "
           "line does not reach the engine (an unparsed line is silently dropped by the tokenizer) "
           "or the records are not a function of the input at all.";
    // invariant: WHICH records a one-line drop leaves alone depends on where the midpoint split
    // lands, so it is reported and never asserted -- pinning it would pin the mutation.
    GTEST_LOG_(INFO) << "[" << corpus.name << "] last-line drop moved " << (kRecordCount - survived)
                     << " of " << kRecordCount << " record(s)."
                     << (moved_names.empty() ? "" : "\n  moved:") << moved_names
                     << (survivor_names.empty() ? "" : "\n  unchanged:") << survivor_names;
}

// invariant: mutation M2 -- swapping the diff operands must not reproduce the diff record.
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

// invariant: mutation M3 -- composing a document with itself must not reproduce the compose record.
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

// invariant: the licence for the failure reporter: prettify is a purely textual reformat, so the
// lines it prints are the record's own bytes rearranged and not a re-encoding.
// note: a prettifier that normalised a number token would make every diff report a plausible lie.
TEST_P(GoldenVector, PrettifiedDiffReportingIsLossless)
{
    const auto& corpus{GetParam()};
    const auto golden{read_vector_file(corpus.name)};
    ASSERT_TRUE(golden.has_value())
        << "golden vector file is missing: " << vector_path(corpus.name).string();
    ASSERT_EQ(golden->size(), static_cast<std::size_t>(kRecordCount));

    for (std::size_t i = 0; i < kRecordCount; ++i)
    {
        // note: minify_json takes a resizable lvalue, so the prettified form is materialised first.
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

// invariant: the one value the vectors froze out of their domain, witnessed here so the freeze
// costs no coverage: at the default config the engine stamps one version constant.
// note: this arm is what fails if the two spellings of that constant ever diverge again.
TEST(GoldenVectorProducerEnvelope, ProducerVersionIsStampedFromTheOnePackageConstant)
{
    const auto lines{cw::read_lines(corpus_path(kCorpora.front().name).string())};
    ASSERT_TRUE(lines.has_value())
        << "corpus file is missing: " << corpus_path(kCorpora.front().name).string();

    meta::MetaLogConfig config;
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
