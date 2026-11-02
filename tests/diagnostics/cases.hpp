#pragma once

#include "gentest/runner.h"
#include "public/gentest_textual_suite_mocks.hpp"

namespace diagnostics_tests {

[[using gentest: test]] void container_failure();
[[using gentest: test]] void string_failure();
[[using gentest: test]] void scoped_failure();
[[using gentest: test]] void scope_unwinding();
[[using gentest: test]] void fatal_matcher();
[[using gentest: test]] void matcher_success();
[[using gentest: test]] void matcher_single_evaluation();
[[using gentest: test]] void native_elements();
[[using gentest: test]] void context_success();

[[using gentest: test]] gentest::awaitable<void> async_contexts();
[[using gentest: test]] gentest::awaitable<void> async_context_success();

[[using gentest: test]] void cancellation_contexts();

[[using gentest: test, parameters(mode, 0, 1, 2, 3, 4, 5)]] void mock_source(int mode);

[[using gentest: test, parameters(mode, 0, 1, 2)]] void matcher_failures(int mode);

} // namespace diagnostics_tests
