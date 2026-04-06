#include "gentest/detail/runtime_context.h"

namespace gentest::detail {

namespace {

thread_local std::shared_ptr<TestContextInfo>           g_current_test{};
thread_local TestContextLocalBuffer                     g_current_buffer{};
thread_local BenchPhase                                 g_bench_phase = BenchPhase::None;
thread_local std::string                                g_bench_error{};
thread_local NoExceptionsFatalHookState                 g_noexceptions_fatal_hook{};
std::atomic<std::underlying_type_t<gentest::LogPolicy>> g_default_log_policy{gentest::to_underlying(gentest::LogPolicy::Never)};

} // namespace

GENTEST_RUNTIME_API auto current_test_storage() -> std::shared_ptr<TestContextInfo> & { return g_current_test; }

GENTEST_RUNTIME_API auto current_buffer_storage() -> TestContextLocalBuffer & { return g_current_buffer; }

GENTEST_RUNTIME_API auto default_log_policy_storage() -> std::atomic<std::underlying_type_t<gentest::LogPolicy>> & {
    return g_default_log_policy;
}

GENTEST_RUNTIME_API auto bench_phase_storage() -> BenchPhase & { return g_bench_phase; }

GENTEST_RUNTIME_API auto bench_error_storage() -> std::string & { return g_bench_error; }

GENTEST_RUNTIME_API auto noexceptions_fatal_hook_storage() -> NoExceptionsFatalHookState & { return g_noexceptions_fatal_hook; }

} // namespace gentest::detail
