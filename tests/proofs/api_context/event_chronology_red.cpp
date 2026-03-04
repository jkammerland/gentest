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

    const auto fail_it = std::find(ctx->event_lines.begin(), ctx->event_lines.end(), std::string("F-before-log"));
    const auto log_it  = std::find(ctx->event_lines.begin(), ctx->event_lines.end(), std::string("L-after-failure"));
    if (fail_it == ctx->event_lines.end() || log_it == ctx->event_lines.end()) {
        std::cerr << "RED: expected both failure and log events to be present\n";
        return 1;
    }

    const auto fail_index = static_cast<std::size_t>(fail_it - ctx->event_lines.begin());
    const auto log_index  = static_cast<std::size_t>(log_it - ctx->event_lines.begin());
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
