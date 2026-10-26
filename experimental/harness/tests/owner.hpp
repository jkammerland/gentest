#pragma once
#include "gentest/attributes.h"
#include "gentest/harness.h"

// Invoked only by the runner-death regression, never registered as a CTest.
[[gentest::test("owner_victim")]] auto ownerVictim() -> gentest::awaitable<>;
