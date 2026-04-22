#pragma once

// Legacy runtime-detail compatibility header. Generated code should include
// `gentest/detail/generated_runtime.h`; this header adds only direct
// shared-fixture setup/teardown entry points used by the runtime and legacy
// detail consumers.

#include "gentest/detail/generated_runtime.h"

#include <vector>

namespace gentest::detail {

// Setup/teardown shared fixtures before/after the test run. setup returns false
// when shared fixture infrastructure fails (for example conflicting
// registrations, allocation failures, or setup failures).
GENTEST_RUNTIME_API bool setup_shared_fixtures();
GENTEST_RUNTIME_API bool teardown_shared_fixtures(std::vector<std::string> *errors = nullptr);

} // namespace gentest::detail
