#pragma once

#include "gentest/detail/runtime_context.h"
#include "recording_internal.h"
#include "runner_output.h"

#include <memory>
#include <string>
#include <string_view>

namespace gentest::runner::detail {

inline auto make_active_test_context(std::string_view display_name) -> std::shared_ptr<gentest::detail::TestContextInfo> {
    auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
    ctx->display_name = std::string(display_name);
    ctx->recording    = gentest::detail::current_recording_target();
    gentest::detail::inherit_framework_context(*ctx);
    gentest::detail::start_context(*ctx);
    ctx->suppress_stdout_log.store(capture_failure_logs(), std::memory_order_release);
    return ctx;
}

class CurrentTestContextScope {
  public:
    explicit CurrentTestContextScope(std::shared_ptr<gentest::detail::TestContextInfo> ctx)
        : previous_(gentest::detail::current_test()), previous_role_(gentest::detail::current_context_role()) {
        gentest::detail::set_current_test(std::move(ctx), gentest::detail::CurrentContextRole::Owner);
    }

    CurrentTestContextScope(const CurrentTestContextScope &)            = delete;
    CurrentTestContextScope &operator=(const CurrentTestContextScope &) = delete;

    ~CurrentTestContextScope() { gentest::detail::set_current_test(std::move(previous_), previous_role_); }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
    gentest::detail::CurrentContextRole               previous_role_;
};

inline void request_active_test_context_stop(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    CurrentTestContextScope current_scope(ctx);
    gentest::detail::request_context_stop(*ctx);
}

template <typename OnWaiting = gentest::detail::IgnoreAdoptedContextWait>
inline void finish_active_test_context(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx, OnWaiting on_waiting = {}) {
    if (!ctx) {
        return;
    }
    request_active_test_context_stop(ctx);
    gentest::detail::wait_for_adopted_contexts(ctx, std::move(on_waiting));
    gentest::detail::clear_context_noexceptions_fatal_hook(ctx);
    gentest::detail::close_context_to_late_operations(*ctx);
    gentest::detail::finish_framework_case(*ctx);
}

inline void deactivate_active_test_context_without_wait(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    request_active_test_context_stop(ctx);
    gentest::detail::clear_context_noexceptions_fatal_hook(ctx);
    gentest::detail::close_context_if_released(*ctx);
}

inline void cancel_active_test_context_without_wait(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    {
        CurrentTestContextScope current_scope(ctx);
        gentest::detail::request_context_stop(*ctx);
        gentest::detail::run_context_cancel_hooks(ctx);
    }
    gentest::detail::clear_context_noexceptions_fatal_hook(ctx);
    gentest::detail::close_context_if_released(*ctx);
}

template <typename OnWaiting = gentest::detail::IgnoreAdoptedContextWait> class BasicCurrentTestScope {
  public:
    explicit BasicCurrentTestScope(std::shared_ptr<gentest::detail::TestContextInfo> ctx, OnWaiting on_waiting = {})
        : ctx_(std::move(ctx)), previous_(gentest::detail::current_test()), previous_role_(gentest::detail::current_context_role()),
          on_waiting_(std::move(on_waiting)) {
        gentest::detail::set_current_test(ctx_, gentest::detail::CurrentContextRole::Owner);
    }

    BasicCurrentTestScope(const BasicCurrentTestScope &)            = delete;
    BasicCurrentTestScope &operator=(const BasicCurrentTestScope &) = delete;

    ~BasicCurrentTestScope() {
        if (!ctx_) {
            return;
        }
        finish_active_test_context(ctx_, std::move(on_waiting_));
        gentest::detail::set_current_test(std::move(previous_), previous_role_);
    }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> ctx_;
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
    gentest::detail::CurrentContextRole               previous_role_;
    // MSVC-compatible layouts use the vendor attribute when it is supported.
#if defined(_MSC_VER) && __has_cpp_attribute(msvc::no_unique_address)
    [[msvc::no_unique_address]]
#elif !defined(_MSC_VER) && __has_cpp_attribute(no_unique_address)
    [[no_unique_address]]
#endif
    OnWaiting on_waiting_;
};

using CurrentTestScope = BasicCurrentTestScope<>;

} // namespace gentest::runner::detail
