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

    bool saw_expected_failure = false;
    bool saw_cleared_log      = false;
    const auto event_count    = std::min(ctx->event_lines.size(), ctx->event_kinds.size());
    for (std::size_t idx = 0; idx < event_count; ++idx) {
        if (ctx->event_kinds[idx] == 'F' && ctx->event_lines[idx].find("F-keep") != std::string::npos) {
            saw_expected_failure = true;
        }
        if (ctx->event_kinds[idx] == 'L' && ctx->event_lines[idx].find("L-before-clear") != std::string::npos) {
            saw_cleared_log = true;
        }
    }

    if (!saw_expected_failure) {
        std::cerr << "FAIL: failure event should remain after clear_logs()\n";
        return 1;
    }
    if (saw_cleared_log) {
        std::cerr << "FAIL: cleared log event should not remain in timeline\n";
        return 1;
    }

    std::cout << "PASS: clear_logs clears buffered logs while preserving failures\n";
    return 0;
}
