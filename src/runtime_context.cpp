#include "gentest/detail/runtime_context.h"

#include <filesystem>

namespace gentest::detail {

namespace {

thread_local std::shared_ptr<TestContextInfo> g_current_test{};
thread_local TestContextLocalBuffer           g_current_buffer{};
thread_local CurrentContextRole               g_current_context_role = CurrentContextRole::None;
thread_local BenchPhase                       g_bench_phase          = BenchPhase::None;
thread_local std::string                      g_bench_error{};
thread_local NoExceptionsFatalHookState       g_noexceptions_fatal_hook{};

auto prepare_current_failure_buffer(std::string_view operation) -> TestContextLocalBuffer & {
    require_owner_context(operation);
    auto  ctx    = current_test_storage();
    auto &buffer = current_buffer_storage();
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

GENTEST_RUNTIME_API auto current_context_role_storage() -> CurrentContextRole & { return g_current_context_role; }

GENTEST_RUNTIME_API auto bench_phase_storage() -> BenchPhase & { return g_bench_phase; }

GENTEST_RUNTIME_API auto bench_error_storage() -> std::string & { return g_bench_error; }

GENTEST_RUNTIME_API auto noexceptions_fatal_hook_storage() -> NoExceptionsFatalHookState & { return g_noexceptions_fatal_hook; }

GENTEST_RUNTIME_API auto install_context_noexceptions_fatal_hook(NoExceptionsFatalHookState state) noexcept
    -> NoExceptionsFatalHookContextToken {
    auto ctx = current_test_storage();
    if (!ctx) {
        return {};
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    auto                        previous = ctx->noexceptions_fatal_hook;
    ctx->noexceptions_fatal_hook         = state;
    return NoExceptionsFatalHookContextToken{.owner = ctx, .previous = previous};
}

GENTEST_RUNTIME_API void restore_context_noexceptions_fatal_hook(const NoExceptionsFatalHookContextToken &token) noexcept {
    auto ctx = token.owner.lock();
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->noexceptions_fatal_hook = token.previous;
}

GENTEST_RUNTIME_API auto has_current_noexceptions_fatal_hook_context() noexcept -> bool {
    return static_cast<bool>(current_test_storage());
}

GENTEST_RUNTIME_API auto take_context_noexceptions_fatal_hook() noexcept -> NoExceptionsFatalHookState {
    auto ctx = current_test_storage();
    if (!ctx) {
        return {};
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    auto                        state = ctx->noexceptions_fatal_hook;
    ctx->noexceptions_fatal_hook      = {};
    return state;
}

GENTEST_RUNTIME_API auto register_context_cancel_hook(ContextCancelHookState state) noexcept -> ContextCancelHookToken {
    auto ctx = current_test_storage();
    if (!ctx || !state.hook) {
        return {};
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    const auto                  id = ++ctx->next_context_cancel_hook_id;
    ctx->context_cancel_hooks.push_back(TestContextInfo::ContextCancelHookEntry{
        .id    = id,
        .state = state,
    });
    return ContextCancelHookToken{.owner = ctx, .id = id};
}

GENTEST_RUNTIME_API void unregister_context_cancel_hook(const ContextCancelHookToken &token) noexcept {
    auto ctx = token.owner.lock();
    if (!ctx || token.id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    auto                       &hooks = ctx->context_cancel_hooks;
    std::erase_if(hooks, [&](const TestContextInfo::ContextCancelHookEntry &entry) { return entry.id == token.id; });
}

GENTEST_RUNTIME_API void run_context_cancel_hooks(const std::shared_ptr<TestContextInfo> &ctx) noexcept {
    if (!ctx) {
        return;
    }
    std::vector<ContextCancelHookState> hooks;
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        for (auto &entry : ctx->context_cancel_hooks) {
            if (entry.ran || !entry.state.hook) {
                continue;
            }
            entry.ran = true;
            hooks.push_back(entry.state);
        }
    }
    for (const auto &hook : hooks) {
        hook.hook(hook.user_data);
    }
}

GENTEST_RUNTIME_API void require_owner_context(std::string_view operation) {
    auto ctx = current_test_storage();
    if (!accepts_late_test_operation(ctx)) {
        fail_without_active_context(operation);
    }
    if (current_context_role_storage() == CurrentContextRole::Owner) {
        return;
    }
    (void)std::fprintf(
        stderr,
        "gentest: fatal: %.*s from an adopted test context.\n"
        "        Worker/adopted contexts may log and observe stop only; report results back and assert on the owning test.\n",
        static_cast<int>(operation.size()), operation.data());
#ifndef NDEBUG
    assert(false && "gentest outcome operation from adopted test context");
#endif
    std::abort();
}

GENTEST_RUNTIME_API void require_not_adopted_context(std::string_view operation) {
    const auto ctx = current_test_storage();
    if (!ctx) {
        return;
    }
    if (current_context_role_storage() == CurrentContextRole::Adopted) {
        (void)std::fprintf(
            stderr,
            "gentest: fatal: %.*s from an adopted test context.\n"
            "        Worker/adopted contexts may log and observe stop only; report results back and assert on the owning test.\n",
            static_cast<int>(operation.size()), operation.data());
#ifndef NDEBUG
        assert(false && "gentest operation from adopted test context");
#endif
        std::abort();
    }
    if (!accepts_late_test_operation(ctx)) {
        fail_without_active_context(operation);
    }
}

void record_failure(std::string msg) {
    auto  ctx    = current_test_storage();
    auto &buffer = prepare_current_failure_buffer("assertion/expectation recorded");
    buffer.failures.push_back(std::move(msg));
    mark_context_failed(*ctx);
    buffer.failure_locations.push_back({.file = std::string{}, .line = 0});
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
    mark_context_failed(*ctx);
    buffer.failure_locations.push_back({.file = normalize_failure_file(std::move(file)), .line = line});
    buffer.event_lines.push_back(buffer.failures.back());
    buffer.event_kinds.push_back('F');
    throw_if_bench_call_failure(buffer.failures.back());
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
