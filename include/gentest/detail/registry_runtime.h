#pragma once

#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/registry_api.h"

namespace gentest::detail {

// Returns an owned, sorted snapshot of the currently registered cases. Used by
// the runtime to keep an active run insulated from later registrations.
GENTEST_RUNTIME_API auto snapshot_registered_cases() -> std::vector<Case>;

} // namespace gentest::detail
