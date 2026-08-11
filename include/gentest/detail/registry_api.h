#pragma once

#include "gentest/detail/case_api.h"

#include <span>
#include <vector>

namespace gentest {

// Runs a caller-owned case table through the standard Gentest CLI and runtime.
// The table is copied for deterministic ordering and is not globally registered.
GENTEST_RUNTIME_API auto run_cases(std::span<const Case> cases, std::span<const char *> args) -> int;

// Unified test entry (argc/argv version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(std::span<const char *> args) -> int;

// Returns an owned, sorted snapshot of the currently registered cases.
GENTEST_RUNTIME_API auto registered_cases() -> std::vector<Case>;

// Explicitly register all test cases from factory functions.
// Called by gentest_main.cpp before run_all_tests().
// Generated code populates the factory registry.
GENTEST_RUNTIME_API void register_all();

} // namespace gentest
