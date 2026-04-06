// Proof test: validate the behavior of always-log controls for runtime context logs.

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

        gentest::always_log_this_test(true);
        gentest::log("always_this_test");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 1 || ctx->event_kinds.size() != 1 || ctx->event_lines.front() != "always_this_test" ||
            ctx->event_kinds.front() != 'L') {
            std::cerr << "RED: expected per-context always-log to capture a log event\n";
            return 1;
        }
        if (ctx->logs.empty() || ctx->logs.front() != "always_this_test") {
            std::cerr << "RED: expected log message to remain in logs payload\n";
            return 1;
        }

        gentest::always_log_this_test(false);
        gentest::log("disabled_this_test");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 1) {
            std::cerr << "RED: expected log event count to remain unchanged when context-only logging is disabled\n";
            return 1;
        }

        gentest::always_log(true);
        gentest::log("global_enabled");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 2 || ctx->event_lines.back() != "global_enabled") {
            std::cerr << "RED: expected global always-log to capture a log event\n";
            return 1;
        }

        gentest::always_log(false);
        gentest::log("global_disabled");
        gentest::detail::flush_current_buffer_for(ctx.get());
        if (ctx->event_lines.size() != 2) {
            std::cerr << "RED: expected no additional events after disabling global always-log\n";
            return 1;
        }

        ctx->active = false;
        gentest::detail::set_current_test(nullptr);
        std::cout << "PASS: context and global always-log controls behave correctly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
