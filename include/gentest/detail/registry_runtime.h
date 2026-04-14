#pragma once

#include "gentest/detail/registry_api.h"

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

// Returns an owned, sorted snapshot of the currently registered cases. Used by
// the runtime to keep an active run insulated from later registrations.
GENTEST_RUNTIME_API auto snapshot_registered_cases() -> std::vector<Case>;

} // namespace gentest::detail
