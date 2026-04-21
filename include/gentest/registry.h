#pragma once

// Compatibility shim: keep the historical include path working for generated
// or handwritten detail users, while `gentest/runner.h` now includes the
// narrower public-only registry API directly.

#include "gentest/detail/fixture_runtime.h"
#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/registry_runtime.h"
