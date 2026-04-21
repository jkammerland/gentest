#pragma once

#include "gentest/detail/case_api.h"
#include "gentest/detail/generated_registration.h"
#include "gentest/detail/runtime_config.h"

#include <span>

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

} // namespace gentest::detail
