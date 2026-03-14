#include "gentest/detail/runtime_context.h"

namespace gentest::detail {

thread_local std::shared_ptr<TestContextInfo> g_current_test{};
thread_local TestContextLocalBuffer           g_current_buffer{};

} // namespace gentest::detail
