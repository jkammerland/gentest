#include "gentest/bench_util.h"

namespace gentest::detail {

// Deliberately opaque to callers: build this file without LTO or unity inclusion.
// No retained pointer, shared state, object access, or per-byte runtime work.
void escape_benchmark_value(void const volatile *) noexcept {}

} // namespace gentest::detail
