// Custom benchmark entry point for insight_metalog. Mirrors
// tokenization/benchmarks/bench_main.cpp.

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

import insight.metalog.bench;

int main(int argc, char** argv)
{
    spdlog::set_level(spdlog::level::off);
    spdlog::default_logger()->set_level(spdlog::level::off);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
