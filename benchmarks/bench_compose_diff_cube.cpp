// NOLINTBEGIN
// bench_compose_diff_cube.cpp — the STAGE-LEVEL cost attribution for the 1.6.0 perf keep-or-kill
// (Part A of technical_docs/architecture/cube_perf_and_collapse.md §A2/§A3). Where the detection
// bench_cube_tick measures the cube's share of the whole matured pyramid tick (the §13 number),
// this isolates the per-call cost of each stage compose()/diff() spend their time in, so the §A2
// three buckets can be split and the §A3 SIMD decision grounded in a number:
//
//   bucket 1 — map/string-union plumbing : compose()/diff() MINUS the reductions MINUS the cube.
//   bucket 2 — reduction tail            : shannon_entropy_bits / divergences / histogram_js
//                                          (the ONLY SIMD-amenable part — contiguous count work).
//   bucket 3 — cube re-closure           : build_closed_cube / compose_cubes / cube_diff_of
//                                          (= the compose()/diff() cube=on − cube=off delta).
//
// The §A3 verdict falls out of comparing bucket 2's primitives against the compose()/diff()
// totals: if the reductions are a thin slice, SIMD on them cannot move the tick and stays parked
// (the lever is the cube — Part B's dimensional-shrink — not vectorising the reduction tail).
//
// Determinism (SPEC §16.9): BM_StageCube_Determinism re-runs build_closed_cube + compose_cubes +
// cube_diff_of and aborts if the content differs across runs. The corpus uses a local splitmix64
// (portable integer RNG, never a std::*_distribution — [[std-distributions-not-cross-stdlib-portable]]).

#include <benchmark/benchmark.h>

import insight.metalog.bench;

namespace
{

namespace meta = insight::metalog;
namespace cube = insight::metalog::cube;
namespace tok = insight::tokenization;
using insight::LogLevel;
using insight::StructuralRole;

struct SplitMix64
{
    std::uint64_t state;
    [[nodiscard]] std::uint64_t next() noexcept
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z{state};
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    [[nodiscard]] std::size_t skewed(std::size_t n) noexcept
    {
        const std::uint64_t x{next() >> 32};
        const std::uint64_t sq{(x * x) >> 32};
        return static_cast<std::size_t>((sq * n) >> 32);
    }
};

constexpr std::size_t kComponentCount{16};
constexpr std::size_t kTemplateCount{256};

// ── representative documents (cube on/off) via the real engine ───────────────────────────────
struct DocCorpus
{
    std::vector<std::string> components;
    std::vector<std::string> templates;
    std::vector<meta::MetaLogDocument> docs;
};

[[nodiscard]] meta::MetaLogConfig stage_config(bool emit_cube) noexcept
{
    meta::MetaLogConfig config;
    config.emit_cube = emit_cube;
    config.top_k_size = 64;
    config.top_ngrams_size = 32;
    config.max_ngram_keys = 4096;
    config.max_param_histograms = 0;
    return config;
}

[[nodiscard]] DocCorpus make_doc_corpus(std::size_t window_count, std::size_t window_size,
                                        bool emit_cube)
{
    DocCorpus corpus;
    for (std::size_t i{0}; i < kComponentCount; ++i)
        corpus.components.push_back("svc_" + std::to_string(i));
    for (std::size_t i{0}; i < kTemplateCount; ++i)
        corpus.templates.push_back("template #" + std::to_string(i) +
                                   " value=<*> latency_ms=<*> code=<*>");

    const meta::MetaLogConfig config{stage_config(emit_cube)};
    const auto base{std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}};
    for (std::size_t w{0}; w < window_count; ++w)
    {
        SplitMix64 rng{0xABCD'1234'ABCD'0000ULL + (w * 0x9E3779B97F4A7C15ULL)};
        meta::MetaLogEngine engine{config};
        const auto start{base + std::chrono::seconds{static_cast<std::int64_t>(w) * 60}};
        engine.open_window(start);
        const std::uint32_t comp_offset{static_cast<std::uint32_t>(w % kComponentCount)};
        for (std::size_t e{0}; e < window_size; ++e)
        {
            tok::CanonicalEvent event;
            const std::uint64_t lvl{rng.next() % 1000U};
            event.level = lvl < 20    ? LogLevel::Fatal
                          : lvl < 100 ? LogLevel::Error
                          : lvl < 220 ? LogLevel::Warn
                          : lvl < 280 ? LogLevel::Debug
                                      : LogLevel::Info;
            event.component = corpus.components[(rng.skewed(kComponentCount) + comp_offset) %
                                                kComponentCount];
            event.template_str = corpus.templates[rng.skewed(kTemplateCount)];
            const std::uint64_t role{rng.next() % 64U};
            event.structural_role = role == 0   ? StructuralRole::Terminator
                                    : role == 1 ? StructuralRole::GroupBegin
                                                : StructuralRole::None;
            engine.ingest_event(event);
        }
        corpus.docs.push_back(engine.close_window(start + std::chrono::seconds{60}));
    }
    return corpus;
}

constexpr std::size_t kStageWindowCount{6};
constexpr std::size_t kStageWindowSize{4'000};

// ── representative reduction-tail inputs (bucket 2) ───────────────────────────────────────────
// Sizes mirror what compose()/diff() actually feed: a ~tail-sized count vector for entropy, and
// two ~vocabulary-sized template→count maps for the divergence over the unified distribution.
[[nodiscard]] std::vector<std::uint64_t> make_count_vector(std::size_t n, std::uint64_t seed)
{
    SplitMix64 rng{seed};
    std::vector<std::uint64_t> counts;
    counts.reserve(n);
    for (std::size_t i{0}; i < n; ++i)
        counts.push_back(1U + (rng.next() % 4096U));
    return counts;
}

[[nodiscard]] std::unordered_map<std::string, std::uint64_t>
make_count_map(std::size_t n, std::uint64_t seed, std::size_t key_shift)
{
    SplitMix64 rng{seed};
    std::unordered_map<std::string, std::uint64_t> counts;
    counts.reserve(n);
    for (std::size_t i{0}; i < n; ++i)
        // key_shift overlaps the two maps partially (a shared vocabulary + a private tail).
        counts.emplace("tmpl_" + std::to_string(i + key_shift), 1U + (rng.next() % 4096U));
    return counts;
}

[[nodiscard]] std::uint64_t total_of(const std::vector<std::uint64_t>& counts) noexcept
{
    std::uint64_t total{0};
    for (const std::uint64_t count : counts)
        total += count;
    return total;
}

[[nodiscard]] std::uint64_t
total_of(const std::unordered_map<std::string, std::uint64_t>& counts) noexcept
{
    std::uint64_t total{0};
    for (const auto& [key, count] : counts)
        total += count;
    return total;
}

// ── compose() / diff() — bucket 1 (no-cube) + bucket 3 delta (cube=on − cube=off) ─────────────
void BM_Compose(benchmark::State& state)
{
    const bool emit_cube{state.range(0) != 0};
    const DocCorpus corpus{make_doc_corpus(kStageWindowCount, kStageWindowSize, emit_cube)};
    std::size_t i{0};
    for (auto _ : state)
    {
        auto composed{meta::compose(corpus.docs[i % corpus.docs.size()],
                                    corpus.docs[(i + 1) % corpus.docs.size()])};
        benchmark::DoNotOptimize(composed);
        ++i;
    }
    state.SetLabel(emit_cube ? "compose cube=on" : "compose cube=off");
}
BENCHMARK(BM_Compose)->Arg(0)->Arg(1)->Unit(benchmark::kMicrosecond);

void BM_Diff(benchmark::State& state)
{
    const bool emit_cube{state.range(0) != 0};
    const DocCorpus corpus{make_doc_corpus(kStageWindowCount, kStageWindowSize, emit_cube)};
    std::size_t i{0};
    for (auto _ : state)
    {
        auto delta{meta::diff(corpus.docs[i % corpus.docs.size()],
                              corpus.docs[(i + 1) % corpus.docs.size()])};
        benchmark::DoNotOptimize(delta);
        ++i;
    }
    state.SetLabel(emit_cube ? "diff cube=on" : "diff cube=off");
}
BENCHMARK(BM_Diff)->Arg(0)->Arg(1)->Unit(benchmark::kMicrosecond);

// ── cube primitives — bucket 3 in isolation ──────────────────────────────────────────────────
[[nodiscard]] std::vector<cube::BaseRow> make_base_rows(std::span<const std::string> components)
{
    // The per-event (level, component, role) joint at representative low cardinality. Owns no
    // strings — component views into the caller's pool (kept alive for build_closed_cube).
    SplitMix64 rng{0x5EED'B45E'0000'0001ULL};
    std::map<std::tuple<LogLevel, std::size_t, StructuralRole>, std::uint64_t> joint;
    for (std::size_t e{0}; e < 20'000; ++e)
    {
        const std::uint64_t lvl{rng.next() % 1000U};
        const LogLevel level{lvl < 30    ? LogLevel::Fatal
                             : lvl < 120 ? LogLevel::Error
                             : lvl < 260 ? LogLevel::Warn
                                         : LogLevel::Info};
        const std::size_t comp{rng.skewed(components.size())};
        const std::uint64_t r{rng.next() % 64U};
        const StructuralRole role{r == 0 ? StructuralRole::Terminator : StructuralRole::None};
        ++joint[std::tuple{level, comp, role}];
    }
    std::vector<cube::BaseRow> rows;
    rows.reserve(joint.size());
    for (const auto& [key, count] : joint)
    {
        const auto& [level, comp, role]{key};
        rows.push_back(
            cube::BaseRow{.level = level, .component = components[comp], .role = role, .count = count});
    }
    return rows;
}

void BM_BuildClosedCube(benchmark::State& state)
{
    std::vector<std::string> components;
    for (std::size_t i{0}; i < kComponentCount; ++i)
        components.push_back("svc_" + std::to_string(i));
    const std::vector<cube::BaseRow> rows{make_base_rows(components)};
    for (auto _ : state)
    {
        auto block{cube::build_closed_cube(rows)};
        benchmark::DoNotOptimize(block);
    }
    state.counters["base_rows"] = benchmark::Counter(static_cast<double>(rows.size()));
}
BENCHMARK(BM_BuildClosedCube)->Unit(benchmark::kMicrosecond);

void BM_ComposeCubes(benchmark::State& state)
{
    const DocCorpus corpus{make_doc_corpus(2, kStageWindowSize, /*emit_cube=*/true)};
    const meta::CubeBlock& lhs{corpus.docs[0].cube};
    const meta::CubeBlock& rhs{corpus.docs[1].cube};
    for (auto _ : state)
    {
        auto composed{cube::compose_cubes(lhs, rhs)};
        benchmark::DoNotOptimize(composed);
    }
    state.counters["lhs_cells"] = benchmark::Counter(static_cast<double>(lhs.cells.size()));
}
BENCHMARK(BM_ComposeCubes)->Unit(benchmark::kMicrosecond);

void BM_CubeDiffOf(benchmark::State& state)
{
    const DocCorpus corpus{make_doc_corpus(2, kStageWindowSize, /*emit_cube=*/true)};
    const meta::CubeBlock& prev{corpus.docs[0].cube};
    const meta::CubeBlock& cur{corpus.docs[1].cube};
    for (auto _ : state)
    {
        auto delta{cube::cube_diff_of(prev, cur)};
        benchmark::DoNotOptimize(delta);
    }
    state.counters["prev_cells"] = benchmark::Counter(static_cast<double>(prev.cells.size()));
}
BENCHMARK(BM_CubeDiffOf)->Unit(benchmark::kMicrosecond);

// ── reduction primitives — bucket 2 (the SIMD-amenable tail) ──────────────────────────────────
// Operating points (the sizes compose()/diff() actually feed): the COMPOSE tail-entropy runs over
// the merged tail (≈ pool − top_k ≈ 192 here); the DIFF divergence runs over counts_of = top_k
// (≤64 per side, union ≤128). The sweeps bracket those so the per-call cost can be read at the
// real point. The reductions are det_log2_fixed (bit-serial integer log2, no libm) — the only
// SIMD-amenable bucket, but per A3 their cost rides det_log2_fixed per element, not the add.
void BM_ShannonEntropy(benchmark::State& state)
{
    const auto n{static_cast<std::size_t>(state.range(0))};
    const std::vector<std::uint64_t> counts{make_count_vector(n, 0x5EED'0001)};
    const std::uint64_t total{total_of(counts)};
    for (auto _ : state)
        benchmark::DoNotOptimize(meta::shannon_entropy_bits(counts, total));
    state.counters["n"] = benchmark::Counter(static_cast<double>(n));
}
BENCHMARK(BM_ShannonEntropy)->Arg(64)->Arg(128)->Arg(192)->Unit(benchmark::kNanosecond);

void BM_Divergences(benchmark::State& state)
{
    const auto n{static_cast<std::size_t>(state.range(0))}; // n = keys PER SIDE (top_k ≈ 64)
    const auto cur{make_count_map(n, 0x5EED'0002, 0)};
    const auto prev{make_count_map(n, 0x5EED'0003, n / 4)}; // 75% vocabulary overlap
    const std::uint64_t cur_total{total_of(cur)};
    const std::uint64_t prev_total{total_of(prev)};
    for (auto _ : state)
        benchmark::DoNotOptimize(meta::divergences(cur, cur_total, prev, prev_total));
    state.counters["n"] = benchmark::Counter(static_cast<double>(n));
}
BENCHMARK(BM_Divergences)->Arg(64)->Arg(128)->Unit(benchmark::kNanosecond);

void BM_HistogramJs(benchmark::State& state)
{
    const auto n{static_cast<std::size_t>(state.range(0))};
    const auto prev{make_count_map(n, 0x5EED'0004, 0)};
    const auto curr{make_count_map(n, 0x5EED'0005, n / 4)};
    const std::uint64_t prev_total{total_of(prev)};
    const std::uint64_t curr_total{total_of(curr)};
    for (auto _ : state)
        benchmark::DoNotOptimize(meta::histogram_js(prev, prev_total, curr, curr_total));
    state.counters["n"] = benchmark::Counter(static_cast<double>(n));
}
BENCHMARK(BM_HistogramJs)->Arg(64)->Unit(benchmark::kNanosecond);

// ── determinism gate ──────────────────────────────────────────────────────────────────────────
void BM_StageCube_Determinism(benchmark::State& state)
{
    std::vector<std::string> components;
    for (std::size_t i{0}; i < kComponentCount; ++i)
        components.push_back("svc_" + std::to_string(i));
    const std::vector<cube::BaseRow> rows{make_base_rows(components)};

    const meta::CubeBlock first{cube::build_closed_cube(rows)};
    const meta::CubeBlock second{cube::build_closed_cube(rows)};
    if (!(first == second))
    {
        std::cerr << "bench_compose_diff_cube DETERMINISM FAILURE: build_closed_cube not "
                     "bit-identical across runs\n";
        std::abort();
    }
    const std::optional<meta::CubeBlock> composed_a{cube::compose_cubes(first, second)};
    const std::optional<meta::CubeBlock> composed_b{cube::compose_cubes(first, second)};
    if (composed_a != composed_b)
    {
        std::cerr << "bench_compose_diff_cube DETERMINISM FAILURE: compose_cubes not "
                     "bit-identical across runs\n";
        std::abort();
    }
    for (auto _ : state)
    {
        auto block{cube::build_closed_cube(rows)};
        benchmark::DoNotOptimize(block);
    }
    state.SetLabel(std::format("closed_cells={} (deterministic across runs)", first.cells.size()));
}
BENCHMARK(BM_StageCube_Determinism)->Iterations(1)->Unit(benchmark::kMicrosecond);

} // namespace

// NOLINTEND
