// Custom benchmark entry point for insight_metalog. Mirrors
// tokenization/benchmarks/bench_main.cpp.

#include <benchmark/benchmark.h>
#include <spdlog/common.h> // spdlog::level — named for init_logging's level choice below

import insight.canon;
import insight.metalog.bench;

int main(int argc, char** argv)
{
    // Re-exec once with ASLR disabled so address-layout randomization does not add run-to-run
    // timing noise (Google Benchmark's "ASLR is enabled" warning). Must precede any other work.
    benchmark::MaybeReenterWithoutASLR(argc, argv);

    // SILENCE, DECLARED (DN-53.D7). A benchmark measures nanoseconds, so an emitted record does not
    // merely land in the wrong place — it perturbs the number. `off` is the level argument's own
    // silencing value; the declaration in canon.api.cppm owns why this is the only door, and why
    // the `spdlog::set_level` + `default_logger()->set_level` pair this replaces stopped reaching
    // canon. Safe under call_once: nothing else in a bench binary calls init_logging.
    insight::logging::init_logging(spdlog::level::off);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
