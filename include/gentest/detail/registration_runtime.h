#pragma once

// Unstable generated-code registration contract. The public runner API sees the
// `Case` model through `case_api.h`, but mutable registration stays here.

#include "gentest/detail/case_api.h"

#include <span>
#include <vector>

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

// Factory function type: returns a vector of cases discovered from one TU.
// Generated companions provide one of these per source file.
using CaseFactory = std::vector<Case> (*)();

// Register cases via a factory function. Called by the generated registry.
GENTEST_RUNTIME_API void register_cases_from_factory(CaseFactory factory);

// Register a factory function. Called by each generated companion.
GENTEST_RUNTIME_API void register_factory(CaseFactory factory);

} // namespace gentest::detail
