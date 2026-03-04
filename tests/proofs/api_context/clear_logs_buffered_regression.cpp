#include "gentest/runner.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

int main() {
    auto ctx          = std::make_shared<gentest::detail::TestContextInfo>();
    ctx->display_name = "proof/clear_logs_buffered_regression";
    ctx->active       = true;
    gentest::detail::set_current_test(ctx);

    gentest::log_on_fail(true);
    gentest::log("L-before-clear");
    gentest::detail::record_failure("F-keep");
    gentest::clear_logs();
    gentest::detail::flush_current_buffer_for(ctx.get());

    ctx->active = false;
    gentest::detail::set_current_test(nullptr);

    if (!ctx->logs.empty()) {
        std::cerr << "FAIL: logs should be empty after clear_logs()\n";
        return 1;
    }

    const auto fail_it = std::find(ctx->event_lines.begin(), ctx->event_lines.end(), std::string("F-keep"));
    const auto log_it  = std::find(ctx->event_lines.begin(), ctx->event_lines.end(), std::string("L-before-clear"));
    if (fail_it == ctx->event_lines.end()) {
        std::cerr << "FAIL: failure event should remain after clear_logs()\n";
        return 1;
    }
    if (log_it != ctx->event_lines.end()) {
        std::cerr << "FAIL: cleared log event should not remain in timeline\n";
        return 1;
    }

    std::cout << "PASS: clear_logs clears buffered logs while preserving failures\n";
    return 0;
}
