#pragma once

#include "gentest/attributes.h"
#include "gentest/harness.h"

namespace harness_signal_mask_tests {
[[gentest::test("harness/session/signal_mask")]] auto blockedCaller() -> gentest::awaitable<>;
} // namespace harness_signal_mask_tests
