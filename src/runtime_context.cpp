#include "gentest/detail/runtime_context.h"

namespace gentest::detail {

GENTEST_RUNTIME_API thread_local std::shared_ptr<TestContextInfo> g_current_test{};
GENTEST_RUNTIME_API thread_local TestContextLocalBuffer           g_current_buffer{};
GENTEST_RUNTIME_API thread_local BenchPhase                       g_bench_phase = BenchPhase::None;
GENTEST_RUNTIME_API thread_local std::string                      g_bench_error{};
GENTEST_RUNTIME_API thread_local NoExceptionsFatalHookState       g_noexceptions_fatal_hook{};

} // namespace gentest::detail
