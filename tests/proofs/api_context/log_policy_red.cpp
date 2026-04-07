// Proof test: validate failure-only logging and per-test overrides of the
// default log policy.

#include "gentest/runner.h"

#include <iostream>
#include <memory>

int main() {
    try {
        auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
        ctx->display_name = "proof/log_policy_red";
        ctx->active       = true;
        gentest::detail::set_current_test(ctx);

        gentest::set_log_policy(gentest::LogPolicy::OnFailure);
        gentest::log("failure_only");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 1 || ctx->event_kinds.size() != 1 || ctx->event_lines.front() != "failure_only" ||
            ctx->event_kinds.front() != 'L') {
            std::cerr << "RED: expected OnFailure policy to capture a failure-only log event\n";
            return 1;
        }

        gentest::set_log_policy(gentest::LogPolicy::OnFailure | gentest::LogPolicy::Always);
        gentest::log("always_override");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 2 || ctx->event_kinds.size() != 2 || ctx->event_lines.back() != "always_override" ||
            ctx->event_kinds.back() != 'A') {
            std::cerr << "RED: expected explicit Always policy to produce a pass-visible log event\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Always);
        gentest::set_log_policy(gentest::LogPolicy::Never);
        gentest::log("explicit_never_override");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 2) {
            std::cerr << "RED: expected explicit policy to override the default log policy\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
        ctx->active = false;
        gentest::detail::set_current_test(nullptr);
        std::cout << "PASS: log policies and overrides behave correctly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
