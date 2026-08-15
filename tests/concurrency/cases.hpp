#pragma once

#include "gentest/runner.h"
#include "gentest/runner_fmt.h"
#include "public/gentest_textual_suite_mocks.hpp"
#include "support/context_proof_support.h"

using namespace gentest::asserts;

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace [[using gentest: suite("concurrency")]] gentest_concurrency_tests {

[[using gentest: test("child_log_pass")]]
void child_log_pass();

[[using gentest: test("child_expect_fail")]]
void child_expect_fail();

[[using gentest: test("child_skip_no_context")]]
void child_skip_no_context();

[[using gentest: test("child_xfail_no_context")]]
void child_xfail_no_context();

[[using gentest: test("child_reports_exception_pass")]]
void child_reports_exception_pass();

[[using gentest: test("multi_adopt_log_pass")]]
void multi_adopt_log_pass();

[[using gentest: test("mock_adopt_dispatch")]]
void mock_adopt_dispatch();

[[using gentest: test("adopted_expect_pass_death")]]
void adopted_expect_pass_death();

[[using gentest: test("adopted_fmt_expect_pass_death")]]
void adopted_fmt_expect_pass_death();

[[using gentest: test("adopted_expect_fail_death")]]
void adopted_expect_fail_death();

[[using gentest: test("adopted_assert_death")]]
void adopted_assert_death();

[[using gentest: test("adopted_expect_throw_death")]]
void adopted_expect_throw_death();

[[using gentest: test("adopted_fail_death")]]
void adopted_fail_death();

[[using gentest: test("adopted_skip_death")]]
void adopted_skip_death();

[[using gentest: test("adopted_xfail_death")]]
void adopted_xfail_death();

[[using gentest: test("adopted_mock_expectation_death")]]
void adopted_mock_expectation_death();

[[using gentest: test("adopted_mock_mode_death")]]
void adopted_mock_mode_death();

[[using gentest: test("adopted_mock_handle_mutation_death")]]
void adopted_mock_handle_mutation_death();

[[using gentest: test("adopted_mock_closed_context_death")]]
void adopted_mock_closed_context_death();

[[using gentest: test("adopted_mock_unexpected_call_death")]]
void adopted_mock_unexpected_call_death();

[[using gentest: test("stop_callback_expect_death")]]
void stop_callback_expect_death();

[[using gentest: test("no_adopt_expect_death_multi")]]
void no_adopt_expect_death_multi();

} // namespace gentest_concurrency_tests
