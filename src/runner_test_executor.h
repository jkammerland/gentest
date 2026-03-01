#pragma once

#include "gentest/runner.h"
#include "runner_reporting.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gentest::runner {

struct TestCounters {
    std::size_t total    = 0;
    std::size_t executed = 0;
    std::size_t passed   = 0;
    std::size_t skipped  = 0;
    std::size_t xfail    = 0;
    std::size_t xpass    = 0;
    std::size_t failed   = 0;
    int         failures = 0;
};

struct TestRunContext {
    bool            color_output   = true;
    bool            record_results = false;
    RunAccumulator *acc            = nullptr;
};

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const std::size_t> idxs, bool shuffle,
                    std::uint64_t base_seed, bool fail_fast, TestCounters &counters);

} // namespace gentest::runner
