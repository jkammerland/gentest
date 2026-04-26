#include "gentest/detail/runtime_context.h"

#include <filesystem>

namespace gentest::detail {

namespace {

thread_local std::shared_ptr<TestContextInfo>           g_current_test{};
thread_local TestContextLocalBuffer                     g_current_buffer{};
thread_local BenchPhase                                 g_bench_phase = BenchPhase::None;
thread_local std::string                                g_bench_error{};
thread_local NoExceptionsFatalHookState                 g_noexceptions_fatal_hook{};
std::atomic<std::underlying_type_t<gentest::LogPolicy>> g_default_log_policy{gentest::to_underlying(gentest::LogPolicy::Never)};

auto prepare_current_failure_buffer(std::string_view operation) -> TestContextLocalBuffer & {
    auto  ctx    = current_test_storage();
    auto &buffer = current_buffer_storage();
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        fail_without_active_context(operation);
    }
    if (buffer.owner != ctx.get()) {
        flush_current_buffer_for(buffer.owner);
        buffer.owner = ctx.get();
    }
    return buffer;
}

auto normalize_failure_file(std::string file) -> std::string {
    if (file.empty()) {
        return {};
    }

    std::filesystem::path p(std::move(file));
    p                     = p.lexically_normal();
    std::string s         = p.generic_string();
    auto        keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) {
            s = s.substr(pos);
            return true;
        }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    return s;
}

void throw_if_bench_call_failure(const std::string &msg) {
#if GENTEST_EXCEPTIONS_ENABLED
    if (bench_phase() == BenchPhase::Call) {
        throw gentest::assertion(msg);
    }
#else
    (void)msg;
#endif
}

} // namespace

GENTEST_RUNTIME_API auto current_test_storage() -> std::shared_ptr<TestContextInfo> & { return g_current_test; }

GENTEST_RUNTIME_API auto current_buffer_storage() -> TestContextLocalBuffer & { return g_current_buffer; }

GENTEST_RUNTIME_API auto default_log_policy_storage() -> std::atomic<std::underlying_type_t<gentest::LogPolicy>> & {
    return g_default_log_policy;
}

GENTEST_RUNTIME_API auto bench_phase_storage() -> BenchPhase & { return g_bench_phase; }

GENTEST_RUNTIME_API auto bench_error_storage() -> std::string & { return g_bench_error; }

GENTEST_RUNTIME_API auto noexceptions_fatal_hook_storage() -> NoExceptionsFatalHookState & { return g_noexceptions_fatal_hook; }

void record_failure(std::string msg) {
    auto  ctx    = current_test_storage();
    auto &buffer = prepare_current_failure_buffer("assertion/expectation recorded");
    buffer.failures.push_back(std::move(msg));
    ctx->has_failures.store(true, std::memory_order_relaxed);
    buffer.failure_locations.push_back({std::string{}, 0});
    buffer.event_lines.push_back(buffer.failures.back());
    buffer.event_kinds.push_back('F');
    throw_if_bench_call_failure(buffer.failures.back());
}

void record_failure(std::string msg, const std::source_location &loc) {
    record_failure_at(std::move(msg), loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()), loc.line());
}

void record_failure_at(std::string msg, std::string file, unsigned line) {
    auto  ctx    = current_test_storage();
    auto &buffer = prepare_current_failure_buffer("assertion/expectation recorded");
    buffer.failures.push_back(std::move(msg));
    ctx->has_failures.store(true, std::memory_order_relaxed);
    buffer.failure_locations.push_back({normalize_failure_file(std::move(file)), line});
    buffer.event_lines.push_back(buffer.failures.back());
    buffer.event_kinds.push_back('F');
    throw_if_bench_call_failure(buffer.failures.back());
}

void record_failure_detail(std::string msg) {
    if (msg.empty()) {
        return;
    }
    auto &buffer = prepare_current_failure_buffer("failure detail recorded");
    buffer.event_lines.push_back(std::move(msg));
    buffer.event_kinds.push_back('L');
}

[[noreturn]] void skip_shared_fixture_unavailable(std::string_view reason, const std::source_location &loc) {
    (void)loc;
    request_runtime_skip(reason, TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
#if GENTEST_EXCEPTIONS_ENABLED
    throw skip_exception{};
#else
    terminate_no_exceptions_fatal("gentest::detail::skip_shared_fixture_unavailable");
#endif
}

} // namespace gentest::detail
