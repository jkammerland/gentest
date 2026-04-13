#include "gentest/runner.h"
#include "support/context_proof_support.h"

#include <algorithm>
#include <iostream>
#include <string>

int main() {
    try {
        gentest::test_support::ActiveProofContext ctx("proof/event_chronology_red");

        gentest::set_log_policy(gentest::LogPolicy::OnFailure);
        gentest::detail::record_failure("F-before-log");
        gentest::log("L-after-failure");
        const auto snapshot = ctx.snapshot();

        std::size_t fail_index  = snapshot.event_lines.size();
        std::size_t log_index   = snapshot.event_lines.size();
        const auto  event_count = std::min(snapshot.event_lines.size(), snapshot.event_kinds.size());
        for (std::size_t idx = 0; idx < event_count; ++idx) {
            if (fail_index == snapshot.event_lines.size() && snapshot.event_kinds[idx] == 'F' &&
                snapshot.event_lines[idx].find("F-before-log") != std::string::npos) {
                fail_index = idx;
            }
            if (log_index == snapshot.event_lines.size() && snapshot.event_kinds[idx] == 'L' &&
                snapshot.event_lines[idx].find("L-after-failure") != std::string::npos) {
                log_index = idx;
            }
        }

        if (fail_index == snapshot.event_lines.size() || log_index == snapshot.event_lines.size()) {
            std::cerr << "RED: expected both failure and log events to be present\n";
            return 1;
        }

        if (fail_index < log_index) {
            std::cout << "PASS: chronology preserved (failure before later log)\n";
            return 0;
        }

        std::cerr << "RED: chronology mismatch (log emitted before earlier failure)\n";
        std::cerr << "fail_index=" << fail_index << ", log_index=" << log_index << '\n';
        for (std::size_t i = 0; i < snapshot.event_lines.size(); ++i) {
            const char kind = i < snapshot.event_kinds.size() ? snapshot.event_kinds[i] : '?';
            std::cerr << "  [" << i << "] kind=" << kind << " line=" << snapshot.event_lines[i] << '\n';
        }
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
