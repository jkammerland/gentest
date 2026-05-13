#include "gentest/runner.h"
#include "support/context_proof_support.h"

#include <algorithm>
#include <iostream>
#include <string>

int main() {
    try {
        gentest::test_support::ActiveProofContext ctx("proof/event_chronology_red");

        gentest::detail::record_failure("F-before-log");
        gentest::log("L-after-failure");
        const auto snapshot = ctx.snapshot();

        std::size_t fail_index  = snapshot.event_lines.size();
        const auto  event_count = std::min(snapshot.event_lines.size(), snapshot.event_kinds.size());
        for (std::size_t idx = 0; idx < event_count; ++idx) {
            if (fail_index == snapshot.event_lines.size() && snapshot.event_kinds[idx] == 'F' &&
                snapshot.event_lines[idx].find("F-before-log") != std::string::npos) {
                fail_index = idx;
            }
        }

        if (fail_index == snapshot.event_lines.size()) {
            std::cerr << "RED: expected failure event to be present\n";
            return 1;
        }
        if (snapshot.event_lines.size() != 1 || snapshot.event_kinds.size() != 1) {
            std::cerr << "RED: expected logs to stay out of the failure event timeline\n";
            return 1;
        }
        if (snapshot.logs.size() != 1 || snapshot.logs.front() != "L-after-failure") {
            std::cerr << "RED: expected log payload to be captured separately\n";
            return 1;
        }

        std::cout << "PASS: failure chronology remains separate from captured logs\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
