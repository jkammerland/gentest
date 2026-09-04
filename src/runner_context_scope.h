#pragma once

#include "gentest/detail/runtime_context.h"
#include "recording_internal.h"

#include <memory>
#include <string>
#include <string_view>

namespace gentest::runner::detail {

inline auto make_active_test_context(std::string_view display_name) -> std::shared_ptr<gentest::detail::TestContextInfo> {
    auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
    ctx->display_name = std::string(display_name);
    ctx->recording    = gentest::detail::current_recording_target();
    gentest::detail::start_context(*ctx);
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

inline void finish_active_test_context(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    request_active_test_context_stop(ctx);
    gentest::detail::wait_for_adopted_contexts(ctx);
    gentest::detail::clear_context_noexceptions_fatal_hook(ctx);
    gentest::detail::close_context_to_late_operations(*ctx);
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

class CurrentTestScope {
  public:
    explicit CurrentTestScope(std::shared_ptr<gentest::detail::TestContextInfo> ctx)
        : ctx_(std::move(ctx)), previous_(gentest::detail::current_test()), previous_role_(gentest::detail::current_context_role()) {
        gentest::detail::set_current_test(ctx_, gentest::detail::CurrentContextRole::Owner);
    }

    CurrentTestScope(const CurrentTestScope &)            = delete;
    CurrentTestScope &operator=(const CurrentTestScope &) = delete;

    ~CurrentTestScope() {
        if (!ctx_) {
            return;
        }
        finish_active_test_context(ctx_);
        gentest::detail::set_current_test(std::move(previous_), previous_role_);
    }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> ctx_;
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
    gentest::detail::CurrentContextRole               previous_role_;
};

} // namespace gentest::runner::detail
