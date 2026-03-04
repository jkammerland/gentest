#include "gentest/runner.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

int main() {
    auto ctx         = std::make_shared<gentest::detail::TestContextInfo>();
    ctx->display_name = "proof/event_chronology_red";
    ctx->active       = true;
    gentest::detail::set_current_test(ctx);

    gentest::log_on_fail(true);
    gentest::detail::record_failure("F-before-log");
    gentest::log("L-after-failure");
    gentest::detail::flush_current_buffer_for(ctx.get());

    ctx->active = false;
    gentest::detail::set_current_test(nullptr);

    std::size_t fail_index = ctx->event_lines.size();
    std::size_t log_index  = ctx->event_lines.size();
    const auto  event_count = std::min(ctx->event_lines.size(), ctx->event_kinds.size());
    for (std::size_t idx = 0; idx < event_count; ++idx) {
        if (fail_index == ctx->event_lines.size() && ctx->event_kinds[idx] == 'F' &&
            ctx->event_lines[idx].find("F-before-log") != std::string::npos) {
            fail_index = idx;
        }
        if (log_index == ctx->event_lines.size() && ctx->event_kinds[idx] == 'L' &&
            ctx->event_lines[idx].find("L-after-failure") != std::string::npos) {
            log_index = idx;
        }
    }

    if (fail_index == ctx->event_lines.size() || log_index == ctx->event_lines.size()) {
        std::cerr << "RED: expected both failure and log events to be present\n";
        return 1;
    }

    if (fail_index < log_index) {
        std::cout << "PASS: chronology preserved (failure before later log)\n";
        return 0;
    }

    std::cerr << "RED: chronology mismatch (log emitted before earlier failure)\n";
    std::cerr << "fail_index=" << fail_index << ", log_index=" << log_index << '\n';
    for (std::size_t i = 0; i < ctx->event_lines.size(); ++i) {
        const char kind = i < ctx->event_kinds.size() ? ctx->event_kinds[i] : '?';
        std::cerr << "  [" << i << "] kind=" << kind << " line=" << ctx->event_lines[i] << '\n';
    }
    return 1;
}
