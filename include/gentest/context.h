#pragma once

#include "gentest/detail/runtime_context.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace gentest {

// Public context adoption API for multi-threaded/coroutine tests.
namespace ctx {

using Token = std::shared_ptr<detail::TestContextInfo>;

inline Token current() { return detail::current_test(); }

struct Adopt {
    Token prev;
    Token adopted;

    Adopt(const Adopt &)            = delete;
    Adopt &operator=(const Adopt &) = delete;

    explicit Adopt(const Token &t) : prev(detail::current_test()), adopted(t) {
        if (adopted) {
            adopted->adopted_tokens.fetch_add(1, std::memory_order_acq_rel);
        }
        detail::set_current_test(adopted);
    }

    // Releasing this guard is part of test completion progress: the runner
    // blocks until all adopted guards for the test context are destroyed.
    ~Adopt() {
        detail::set_current_test(prev);
        if (adopted && adopted->adopted_tokens.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            adopted->adopted_cv.notify_all();
        }
    }
};

} // namespace ctx

// Lightweight per-test logging; appended to failure messages when enabled.
inline void log(std::string_view message) {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    if (detail::g_current_buffer.owner != ctx.get()) {
        detail::flush_current_buffer_for(detail::g_current_buffer.owner);
        detail::g_current_buffer.owner = ctx.get();
    }

    bool dump_logs_on_failure = false;
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        dump_logs_on_failure = ctx->dump_logs_on_failure;
    }

    detail::g_current_buffer.logs.emplace_back(message);
    if (dump_logs_on_failure) {
        detail::g_current_buffer.event_lines.emplace_back(message);
        detail::g_current_buffer.event_kinds.push_back('L');
    }
}

inline void log_on_fail(bool enable = true) {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->dump_logs_on_failure = enable;
}

[[noreturn]] inline void skip(std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    (void)loc;
    detail::request_runtime_skip(reason, detail::TestContextInfo::RuntimeSkipKind::User);
#if GENTEST_EXCEPTIONS_ENABLED
    throw detail::skip_exception{};
#else
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::skip");
#endif
}

inline void skip_if(bool condition, std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    if (condition)
        skip(reason, loc);
}

inline void xfail(std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    (void)loc;
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: xfail called without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->xfail_requested = true;
    if (!reason.empty())
        ctx->xfail_reason = std::string(reason);
}

inline void xfail_if(bool condition, std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    if (condition)
        xfail(reason, loc);
}

} // namespace gentest
