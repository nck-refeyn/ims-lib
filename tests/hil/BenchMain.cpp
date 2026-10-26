// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "HilTarget.h"
#include "TestSupport.h"

#include <benchmark/benchmark.h>

int main(int argc, char** argv)
{
    ims_test::ConfigureLogging();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;

    // Recorded in the JSON "context" block so results can be traced to the rig they came from
    if (auto* t = hil::Acquire()) {
        benchmark::AddCustomContext("hil_target", t->description);
        benchmark::AddCustomContext("hil_simulated", t->simulated ? "true" : "false");
    }
    benchmark::AddCustomContext("hil_writes_allowed", hil::WritesAllowed() ? "true" : "false");

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    hil::Release();
    return 0;
}
