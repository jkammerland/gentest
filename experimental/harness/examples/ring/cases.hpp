#pragma once

#include "gentest/attributes.h"
#include "gentest/harness.h"

namespace ring_example {
[[gentest::test("ring/three_peers")]] auto       threePeers() -> gentest::awaitable<>;
[[gentest::test("ring/four_peers")]] auto        fourPeers() -> gentest::awaitable<>;
[[gentest::test("ring/sixteen_peers")]] auto     sixteenPeers() -> gentest::awaitable<>;
[[gentest::test("ring/blocked_link")]] auto      blockedLink() -> gentest::awaitable<>;
[[gentest::test("ring/packet_validation")]] void packetValidation();
} // namespace ring_example
