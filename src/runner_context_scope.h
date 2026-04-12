#pragma once

#include "gentest/detail/runtime_context.h"

#include <memory>
#include <string>
#include <string_view>

namespace gentest::runner::detail {

inline auto make_active_test_context(std::string_view display_name) -> std::shared_ptr<gentest::detail::TestContextInfo> {
    auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
    ctx->display_name = std::string(display_name);
    ctx->active       = true;
    return ctx;
}

class CurrentTestScope {
  public:
    explicit CurrentTestScope(std::shared_ptr<gentest::detail::TestContextInfo> ctx)
        : ctx_(std::move(ctx)), previous_(gentest::detail::current_test()) {
        gentest::detail::set_current_test(ctx_);
    }

    CurrentTestScope(const CurrentTestScope &)            = delete;
    CurrentTestScope &operator=(const CurrentTestScope &) = delete;

    ~CurrentTestScope() {
        if (!ctx_) {
            return;
        }
        gentest::detail::wait_for_adopted_tokens(ctx_);
        ctx_->active = false;
        gentest::detail::set_current_test(std::move(previous_));
    }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> ctx_;
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
};

} // namespace gentest::runner::detail
