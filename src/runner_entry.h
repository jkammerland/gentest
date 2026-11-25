#pragma once

#if defined(_WIN32)
#include "gentest/detail/runtime_export.h"

namespace gentest::detail {

GENTEST_RUNTIME_API auto runWindowsMain() -> int;

} // namespace gentest::detail
#endif
