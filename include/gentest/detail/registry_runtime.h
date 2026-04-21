#pragma once

// Legacy runtime-detail compatibility header for registry snapshots. Generated
// code should include `gentest/detail/registration_runtime.h` or
// `gentest/detail/generated_runtime.h` for registration mutation.

#include "gentest/detail/registry_api.h"

namespace gentest::detail {

// Returns an owned, sorted snapshot of the currently registered cases. Used by
// the runtime to keep an active run insulated from later registrations.
GENTEST_RUNTIME_API auto snapshot_registered_cases() -> std::vector<Case>;

} // namespace gentest::detail
