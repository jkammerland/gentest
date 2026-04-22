#pragma once

// Unstable generated-code registration contract. The public runner API sees the
// `Case` model through `case_api.h`, but mutable registration stays here.

#include "gentest/detail/case_api.h"

#include <span>

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

} // namespace gentest::detail
