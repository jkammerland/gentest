// Proof test: validate explicit and default always-visible log policies.

#include "gentest/runner.h"

#include <iostream>
#include <memory>
#include <string>

int main() {
    try {
        auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
        ctx->display_name = "proof/always_log_red";
        ctx->active       = true;
        gentest::detail::set_current_test(ctx);

        gentest::set_log_policy(gentest::LogPolicy::Always);
        gentest::log("always_this_test");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 1 || ctx->event_kinds.size() != 1 || ctx->event_lines.front() != "always_this_test" ||
            ctx->event_kinds.front() != 'A') {
            std::cerr << "RED: expected per-context always-log to capture a log event\n";
            return 1;
        }
        if (ctx->logs.empty() || ctx->logs.front() != "always_this_test") {
            std::cerr << "RED: expected log message to remain in logs payload\n";
            return 1;
        }

        gentest::set_log_policy(gentest::LogPolicy::Never);
        gentest::log("disabled_this_test");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 1) {
            std::cerr << "RED: expected log event count to remain unchanged when explicit policy disables pass output\n";
            return 1;
        }

        ctx->active = false;
        gentest::detail::set_current_test(nullptr);

        auto default_ctx          = std::make_shared<gentest::detail::TestContextInfo>();
        default_ctx->display_name = "proof/default_log_policy_red";
        default_ctx->active       = true;
        gentest::detail::set_current_test(default_ctx);

        gentest::set_default_log_policy(gentest::LogPolicy::Always);
        gentest::log("global_enabled");
        gentest::detail::flush_current_buffer_for(default_ctx.get());
        if (default_ctx->event_lines.size() != 1 || default_ctx->event_kinds.size() != 1 ||
            default_ctx->event_lines.back() != "global_enabled" || default_ctx->event_kinds.back() != 'A') {
            std::cerr << "RED: expected default always policy to capture a log event\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
        gentest::log("global_disabled");
        gentest::detail::flush_current_buffer_for(default_ctx.get());
        if (default_ctx->event_lines.size() != 1) {
            std::cerr << "RED: expected no additional events after disabling the default always policy\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
        default_ctx->active = false;
        gentest::detail::set_current_test(nullptr);
        std::cout << "PASS: explicit and default always-visible policies behave correctly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
