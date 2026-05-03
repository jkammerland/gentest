#pragma once

#include "gentest/runner.h"
#include "runner_test_executor.h"
#include "runner_test_plan.h"

#include <span>

namespace gentest::runner {

bool plans_include_async_cases(std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans);
bool run_tests_async_batch(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans,
                           bool fail_fast, TestCounters &counters);

} // namespace gentest::runner
