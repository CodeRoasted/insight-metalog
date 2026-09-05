#include <benchmark/benchmark.h>
#include <spdlog/common.h>

import insight.canon;
import insight.metalog.bench;

int main(int argc, char** argv)
{
    // pre: benchmark::Initialize has not run yet -- it consumes and rewrites argc and argv.
    // note: ASLR adds run-to-run address-layout noise to a nanosecond measurement.
    benchmark::MaybeReenterWithoutASLR(argc, argv);

    // invariant: logging is silenced for the whole binary -- an emitted record does not merely land
    // in the wrong place, it perturbs the number a benchmark reports.
    // note: safe under call_once because nothing else in a bench binary calls init_logging.
    // refs: DN-53.D7
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
