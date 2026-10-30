#pragma once
#include "gentest/attributes.h"
#include "gentest/harness.h"

namespace harness_tests {
[[gentest::test("harness/ipc/queue")]] void                  queue();
[[gentest::test("harness/ipc/process")]] void                process();
[[gentest::test("harness/ipc/owner_death")]] void            channelOwnerDeath();
[[gentest::test("harness/session/runner_death")]] void       runnerDeath();
[[gentest::test("harness/session/composition")]] auto        composition() -> gentest::awaitable<>;
[[gentest::test("harness/session/failures")]] auto           failures() -> gentest::awaitable<>;
[[gentest::test("harness/session/startup")]] auto            startup() -> gentest::awaitable<>;
[[gentest::test("harness/session/descriptor_limits")]] auto  descriptorLimits() -> gentest::awaitable<>;
[[gentest::test("harness/session/cleanup")]] auto            cleanup() -> gentest::awaitable<>;
[[gentest::test("harness/network/stateful_nat")]] auto       statefulNat() -> gentest::awaitable<>;
[[gentest::test("harness/network/forwarding")]] auto         forwarding() -> gentest::awaitable<>;
[[gentest::test("harness/network/forwarding_failure")]] auto forwardingFailure() -> gentest::awaitable<>;
} // namespace harness_tests
