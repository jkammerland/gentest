#pragma once

#include "gentest/runner.h"
#include "runner_case_invoker.h"
#include "runner_result_model.h"
#include "runner_test_executor.h"

#include <string>
#include <string_view>

namespace gentest::runner {

long long duration_ms(double seconds);

RunResult make_static_skip_result(TestRunContext &state, const gentest::Case &test, TestCounters &c);
RunResult finish_invoke_result(TestRunContext &state, const gentest::Case &test, const InvokeResult &inv, TestCounters &c);
RunResult execute_one(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c);

void execute_and_record(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c);
void record_synthetic_skip(TestRunContext &state, const gentest::Case &test, std::string reason, TestCounters &c,
                           bool infra_failure = false);

std::string shared_fixture_unavailable_message(std::string_view fixture, std::string reason);

} // namespace gentest::runner
