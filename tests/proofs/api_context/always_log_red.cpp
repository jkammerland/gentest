// Proof test: validate explicit and default always-visible log policies.

#include "gentest/runner.h"
#include "support/context_proof_support.h"

#include <iostream>
#include <string>

int main() {
    try {
        gentest::test_support::ActiveProofContext ctx("proof/always_log_red");

        gentest::set_log_policy(gentest::LogPolicy::Always);
        gentest::log("always_this_test");
        auto snapshot = ctx.snapshot();
        if (snapshot.event_lines.size() != 1 || snapshot.event_kinds.size() != 1 || snapshot.event_lines.front() != "always_this_test" ||
            snapshot.event_kinds.front() != 'A') {
            std::cerr << "RED: expected per-context always-log to capture a log event\n";
            return 1;
        }
        if (snapshot.logs.empty() || snapshot.logs.front() != "always_this_test") {
            std::cerr << "RED: expected log message to remain in logs payload\n";
            return 1;
        }

        gentest::set_log_policy(gentest::LogPolicy::Never);
        gentest::log("disabled_this_test");
        snapshot = ctx.snapshot();
        if (snapshot.event_lines.size() != 1) {
            std::cerr << "RED: expected log event count to remain unchanged when explicit policy disables pass output\n";
            return 1;
        }

        gentest::test_support::ActiveProofContext default_ctx("proof/default_log_policy_red");

        gentest::set_default_log_policy(gentest::LogPolicy::Always);
        gentest::log("global_enabled");
        snapshot = default_ctx.snapshot();
        if (snapshot.event_lines.size() != 1 || snapshot.event_kinds.size() != 1 || snapshot.event_lines.back() != "global_enabled" ||
            snapshot.event_kinds.back() != 'A') {
            std::cerr << "RED: expected default always policy to capture a log event\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
        gentest::log("global_disabled");
        snapshot = default_ctx.snapshot();
        if (snapshot.event_lines.size() != 1) {
            std::cerr << "RED: expected no additional events after disabling the default always policy\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
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
