#pragma once

#include "gentest/detail/case_api.h"

#include <span>
#include <vector>

namespace gentest {

// Unified test entry (argc/argv version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(std::span<const char *> args) -> int;

// Returns an owned, sorted snapshot of the currently registered cases.
GENTEST_RUNTIME_API auto registered_cases() -> std::vector<Case>;

} // namespace gentest
