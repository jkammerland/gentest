#pragma once

#include "gentest/runner.h"
#include "runner_reporting.h"
#include "runner_test_plan.h"

#include <cstddef>
#include <span>

namespace gentest::runner {

struct TestCounters {
    std::size_t total    = 0;
    std::size_t passed   = 0;
    std::size_t skipped  = 0;
    std::size_t blocked  = 0;
    std::size_t xfail    = 0;
    std::size_t xpass    = 0;
    std::size_t failed   = 0;
    int         failures = 0;
};

struct TestRunContext {
    bool            color_output         = true;
    bool            record_results       = false;
    bool            suppress_case_output = false;
    RunAccumulator *acc                  = nullptr;
};

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans, bool fail_fast,
                    TestCounters &counters);

} // namespace gentest::runner
