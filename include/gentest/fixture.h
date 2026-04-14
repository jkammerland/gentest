#pragma once

// Compatibility shim: keep the historical include path working for generated
// or handwritten detail users, while `gentest/runner.h` now includes the
// narrower public-only fixture API directly.

#include "gentest/detail/fixture_api.h"
#include "gentest/detail/fixture_runtime.h"
