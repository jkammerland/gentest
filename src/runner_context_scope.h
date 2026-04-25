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

class CurrentTestAdoptionScope {
  public:
    explicit CurrentTestAdoptionScope(std::shared_ptr<gentest::detail::TestContextInfo> ctx) : previous_(gentest::detail::current_test()) {
        gentest::detail::set_current_test(std::move(ctx));
    }

    CurrentTestAdoptionScope(const CurrentTestAdoptionScope &)            = delete;
    CurrentTestAdoptionScope &operator=(const CurrentTestAdoptionScope &) = delete;

    ~CurrentTestAdoptionScope() { gentest::detail::set_current_test(std::move(previous_)); }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
};

inline void finish_active_test_context(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
    if (!ctx) {
        return;
    }
    gentest::detail::wait_for_adopted_contexts(ctx);
    ctx->active = false;
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
        finish_active_test_context(ctx_);
        gentest::detail::set_current_test(std::move(previous_));
    }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> ctx_;
    std::shared_ptr<gentest::detail::TestContextInfo> previous_;
};

} // namespace gentest::runner::detail
