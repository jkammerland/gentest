#pragma once

#include "gentest/detail/runtime_context.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest::test_support {

struct ContextSnapshot {
    std::vector<std::string> logs;
    std::vector<std::string> event_lines;
    std::vector<char>        event_kinds;
};

class ActiveProofContext {
  public:
    explicit ActiveProofContext(std::string_view display_name)
        : previous_(gentest::detail::current_test()), previous_role_(gentest::detail::current_context_role()),
          ctx_(std::make_shared<gentest::detail::TestContextInfo>()) {
        ctx_->display_name = std::string(display_name);
        gentest::detail::start_context(*ctx_);
        gentest::detail::set_current_test(ctx_, gentest::detail::CurrentContextRole::Owner);
    }

    ActiveProofContext(const ActiveProofContext &)            = delete;
    ActiveProofContext &operator=(const ActiveProofContext &) = delete;

    ~ActiveProofContext() {
        if (ctx_) {
            gentest::detail::request_context_stop(*ctx_);
            gentest::detail::wait_for_adopted_contexts(ctx_);
            gentest::detail::close_context_to_late_operations(*ctx_);
        }
        gentest::detail::set_current_test(std::move(previous_), previous_role_);
    }

    auto snapshot() const -> ContextSnapshot {
        gentest::detail::flush_current_buffer_for(ctx_.get());
        return {
            .logs        = ctx_->logs,
            .event_lines = ctx_->event_lines,
            .event_kinds = ctx_->event_kinds,
        };
    }

  private:
    std::shared_ptr<gentest::detail::TestContextInfo> previous_{};
    gentest::detail::CurrentContextRole               previous_role_{gentest::detail::CurrentContextRole::None};
    std::shared_ptr<gentest::detail::TestContextInfo> ctx_{};
};

} // namespace gentest::test_support
