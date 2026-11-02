#pragma once

// Legacy runtime-detail compatibility header. Generated code should include
// `gentest/detail/generated_runtime.h`; this header adds only direct
// shared-fixture setup/teardown entry points used by the runtime and legacy
// detail consumers.

#include "gentest/detail/generated_runtime.h"

#include <vector>

namespace gentest::detail {

// Begin/end shared fixture state for the active runtime session. Setup checks
// registrations; get_shared_fixture activates an instance and its dependencies
// on first acquisition. Teardown visits only activated instances.
GENTEST_RUNTIME_API bool setup_shared_fixtures();
GENTEST_RUNTIME_API bool teardown_shared_fixtures(std::vector<std::string> *errors = nullptr);

} // namespace gentest::detail
