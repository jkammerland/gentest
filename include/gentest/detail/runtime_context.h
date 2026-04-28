#pragma once

#include "gentest/detail/runtime_base.h"
#include "gentest/detail/runtime_support.h"
#include "gentest/log_policy.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest::detail {

struct TestContextInfo {
    std::string              display_name;
    std::vector<std::string> failures;
    struct FailureLoc {
        std::string file;
        unsigned    line = 0;
    };
    std::vector<FailureLoc>  failure_locations;
    std::vector<std::string> logs;
    // Chronological event stream for console/reporting.
    // kind: 'F' failure, 'L' failure-only log, 'A' always-visible log
    std::vector<std::string> event_lines;
    std::vector<char>        event_kinds;
    std::mutex               mtx;
    std::mutex               adopted_mtx;
    std::condition_variable  adopted_cv;
    struct AdoptedReleaseWake {
        void notify_one() noexcept { cv.notify_one(); }
        void notify_all() noexcept { cv.notify_all(); }

        std::condition_variable cv;
    };
    std::vector<std::weak_ptr<AdoptedReleaseWake>> adopted_release_wakes;
    NoExceptionsFatalHookState                     noexceptions_fatal_hook;
    struct ContextCancelHookEntry {
        std::size_t            id = 0;
        ContextCancelHookState state;
        bool                   ran = false;
    };
    std::vector<ContextCancelHookEntry> context_cancel_hooks;
    std::size_t                         next_context_cancel_hook_id = 0;
    std::atomic<bool>                   active{false};
    std::atomic<bool>                   canceled{false};
    std::atomic<bool>                   has_failures{false};
    std::atomic<std::size_t>            adopted_contexts{0};
    gentest::LogPolicy                  log_policy{gentest::LogPolicy::Never};
    bool                                log_policy_overridden{false};

    std::atomic<bool> runtime_skip_requested{false};
    std::string       runtime_skip_reason;
    enum class RuntimeSkipKind {
        User,
        SharedFixtureInfra,
    };
    RuntimeSkipKind runtime_skip_kind{RuntimeSkipKind::User};

    bool        xfail_requested{false};
    std::string xfail_reason;
};

struct TestContextLocalBuffer {
    TestContextInfo *owner = nullptr;

    std::vector<std::string>                 failures;
    std::vector<TestContextInfo::FailureLoc> failure_locations;
    std::vector<std::string>                 logs;
    std::vector<std::string>                 event_lines;
    std::vector<char>                        event_kinds;

    bool empty() const {
        return failures.empty() && failure_locations.empty() && logs.empty() && event_lines.empty() && event_kinds.empty();
    }

    void clear() {
        failures.clear();
        failure_locations.clear();
        logs.clear();
        event_lines.clear();
        event_kinds.clear();
    }
};

GENTEST_RUNTIME_API auto current_test_storage() -> std::shared_ptr<TestContextInfo> &;
GENTEST_RUNTIME_API auto current_buffer_storage() -> TestContextLocalBuffer &;
GENTEST_RUNTIME_API auto default_log_policy_storage() -> std::atomic<std::underlying_type_t<gentest::LogPolicy>> &;

[[noreturn]] inline void fail_without_active_context(std::string_view operation) {
    (void)std::fprintf(stderr,
                       "gentest: fatal: %.*s without an active test context.\n"
                       "        Did you forget to set the current context in this thread/coroutine?\n",
                       static_cast<int>(operation.size()), operation.data());
#ifndef NDEBUG
    assert(false && "gentest operation without an active test context");
#endif
    std::abort();
}

template <class T> inline void append_moved(std::vector<T> &dst, std::vector<T> &src) {
    if (src.empty()) {
        return;
    }
    dst.reserve(dst.size() + src.size());
    for (auto &value : src) {
        dst.push_back(std::move(value));
    }
}

inline void flush_current_buffer_for(TestContextInfo *ctx) {
    auto &buffer = current_buffer_storage();
    if (!ctx || buffer.owner != ctx || buffer.empty())
        return;

    std::lock_guard<std::mutex> lk(ctx->mtx);
    append_moved(ctx->failures, buffer.failures);
    append_moved(ctx->failure_locations, buffer.failure_locations);
    append_moved(ctx->logs, buffer.logs);
    append_moved(ctx->event_lines, buffer.event_lines);
    append_moved(ctx->event_kinds, buffer.event_kinds);
    buffer.clear();
}

inline void set_current_test(std::shared_ptr<TestContextInfo> ctx) {
    auto &current_test = current_test_storage();
    auto &buffer       = current_buffer_storage();
    if (current_test) {
        flush_current_buffer_for(current_test.get());
    }
    current_test = std::move(ctx);
    buffer.owner = current_test ? current_test.get() : nullptr;
}

inline std::shared_ptr<TestContextInfo> current_test() { return current_test_storage(); }

inline bool accepts_late_test_operation(const std::shared_ptr<TestContextInfo> &ctx) {
    if (!ctx) {
        return false;
    }
    if (ctx->active.load(std::memory_order_acquire)) {
        return true;
    }
    return ctx->canceled.load(std::memory_order_acquire);
}

inline void close_canceled_context_if_released(TestContextInfo &ctx) noexcept {
    if (ctx.adopted_contexts.load(std::memory_order_acquire) != 0) {
        return;
    }
    if (ctx.active.load(std::memory_order_acquire)) {
        return;
    }
    ctx.canceled.store(false, std::memory_order_release);
}

inline void wait_for_adopted_contexts(const std::shared_ptr<TestContextInfo> &ctx) {
    if (!ctx)
        return;
    // Intentional barrier: test/phase completion waits for all adopted contexts
    // to be released. If adopted work is leaked (for example detached/stuck),
    // completion blocks by design to preserve one-shot outcome accounting.
    std::unique_lock<std::mutex> lk(ctx->adopted_mtx);
    ctx->adopted_cv.wait(lk, [&] { return ctx->adopted_contexts.load(std::memory_order_acquire) == 0; });
}

inline void clear_context_noexceptions_fatal_hook(const std::shared_ptr<TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->noexceptions_fatal_hook = {};
}

inline void register_adopted_release_wake(const std::shared_ptr<TestContextInfo>                     &ctx,
                                          const std::shared_ptr<TestContextInfo::AdoptedReleaseWake> &wake) {
    if (!ctx || !wake) {
        return;
    }
    std::lock_guard<std::mutex> lk(ctx->adopted_mtx);
    auto                       &wakes = ctx->adopted_release_wakes;
    for (auto it = wakes.begin(); it != wakes.end();) {
        if (it->expired()) {
            it = wakes.erase(it);
        } else {
            ++it;
        }
    }
    wakes.push_back(wake);
}

inline void notify_adopted_contexts_released(TestContextInfo &ctx) noexcept {
    std::vector<std::shared_ptr<TestContextInfo::AdoptedReleaseWake>> wakes;
    {
        std::lock_guard<std::mutex> lk(ctx.adopted_mtx);
        auto                       &registered_wakes = ctx.adopted_release_wakes;
        for (auto it = registered_wakes.begin(); it != registered_wakes.end();) {
            if (auto wake = it->lock()) {
                wakes.push_back(std::move(wake));
                ++it;
            } else {
                it = registered_wakes.erase(it);
            }
        }
    }
    for (auto &wake : wakes) {
        wake->notify_all();
    }
    ctx.adopted_cv.notify_all();
}

inline std::string first_recorded_failure(const std::shared_ptr<TestContextInfo> &ctx) {
    if (!ctx)
        return {};

    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (ctx->failures.empty()) {
        return {};
    }
    return ctx->failures.front();
}

inline void request_runtime_skip(std::string_view reason, TestContextInfo::RuntimeSkipKind kind) {
    auto ctx = current_test_storage();
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        fail_without_active_context("skip called");
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->runtime_skip_requested.store(true, std::memory_order_relaxed);
    ctx->runtime_skip_reason = std::string(reason);
    ctx->runtime_skip_kind   = kind;
}

} // namespace gentest::detail
