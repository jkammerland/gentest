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

    explicit Adopt(Token t) : prev(detail::current_test()), adopted(std::move(t)) {
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

// Lightweight per-test logging.
// - `set_log_policy()` overrides log visibility for the active test context.
// - `set_default_log_policy()` controls the process-global default when a test
//   does not override it explicitly.
inline void log(std::string_view message) {
    auto  ctx    = detail::current_test_storage();
    auto &buffer = detail::current_buffer_storage();
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    if (buffer.owner != ctx.get()) {
        detail::flush_current_buffer_for(buffer.owner);
        buffer.owner = ctx.get();
    }

    LogPolicy  policy            = LogPolicy::Never;
    bool       policy_overridden = false;
    const auto always_bits       = gentest::to_underlying(LogPolicy::Always);
    const auto on_failure_bits   = gentest::to_underlying(LogPolicy::OnFailure);
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        policy            = ctx->log_policy;
        policy_overridden = ctx->log_policy_overridden;
    }
    if (!policy_overridden) {
        policy = static_cast<LogPolicy>(detail::default_log_policy_storage().load(std::memory_order_acquire));
    }

    buffer.logs.emplace_back(message);
    const auto policy_bits = gentest::to_underlying(policy);
    if ((policy_bits & always_bits) == always_bits) {
        buffer.event_lines.emplace_back(message);
        buffer.event_kinds.push_back('A');
    } else if ((policy_bits & on_failure_bits) != 0) {
        buffer.event_lines.emplace_back(message);
        buffer.event_kinds.push_back('L');
    }
}

inline void set_log_policy(LogPolicy policy) {
    auto ctx = detail::current_test_storage();
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->log_policy            = policy;
    ctx->log_policy_overridden = true;
}

inline void set_default_log_policy(LogPolicy policy) {
    detail::default_log_policy_storage().store(gentest::to_underlying(policy), std::memory_order_release);
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
    auto ctx = detail::current_test_storage();
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        (void)std::fputs("gentest: fatal: xfail called without an active test context.\n"
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
