#include "gentest/detail/runtime_context.h"

namespace gentest::detail {

GENTEST_RUNTIME_API thread_local std::shared_ptr<TestContextInfo> g_current_test{};
GENTEST_RUNTIME_API thread_local TestContextLocalBuffer           g_current_buffer{};

} // namespace gentest::detail
