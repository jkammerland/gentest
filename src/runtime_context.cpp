#include "gentest/detail/runtime_context.h"

namespace gentest::detail {

namespace {

thread_local std::shared_ptr<TestContextInfo> g_current_test{};
thread_local TestContextLocalBuffer           g_current_buffer{};
thread_local BenchPhase                       g_bench_phase = BenchPhase::None;
thread_local std::string                      g_bench_error{};
thread_local NoExceptionsFatalHookState       g_noexceptions_fatal_hook{};
std::atomic<bool>                             g_always_log{false};

} // namespace

GENTEST_RUNTIME_API auto current_test_storage() -> std::shared_ptr<TestContextInfo> & { return g_current_test; }

GENTEST_RUNTIME_API auto current_buffer_storage() -> TestContextLocalBuffer & { return g_current_buffer; }

GENTEST_RUNTIME_API auto always_log_storage() -> std::atomic<bool> & { return g_always_log; }

GENTEST_RUNTIME_API auto bench_phase_storage() -> BenchPhase & { return g_bench_phase; }

GENTEST_RUNTIME_API auto bench_error_storage() -> std::string & { return g_bench_error; }

GENTEST_RUNTIME_API auto noexceptions_fatal_hook_storage() -> NoExceptionsFatalHookState & { return g_noexceptions_fatal_hook; }

} // namespace gentest::detail
