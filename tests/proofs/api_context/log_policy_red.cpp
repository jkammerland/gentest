// Proof test: validate failure-only logging and per-test overrides of the
// default log policy.

#include "gentest/runner.h"
#include "support/context_proof_support.h"

#include <iostream>

int main() {
    try {
        gentest::test_support::ActiveProofContext ctx("proof/log_policy_red");

        gentest::set_log_policy(gentest::LogPolicy::OnFailure);
        gentest::log("failure_only");
        auto snapshot = ctx.snapshot();
        if (snapshot.event_lines.size() != 1 || snapshot.event_kinds.size() != 1 || snapshot.event_lines.front() != "failure_only" ||
            snapshot.event_kinds.front() != 'L') {
            std::cerr << "RED: expected OnFailure policy to capture a failure-only log event\n";
            return 1;
        }

        gentest::set_log_policy(gentest::LogPolicy::OnFailure | gentest::LogPolicy::Always);
        gentest::log("always_override");
        snapshot = ctx.snapshot();
        if (snapshot.event_lines.size() != 2 || snapshot.event_kinds.size() != 2 || snapshot.event_lines.back() != "always_override" ||
            snapshot.event_kinds.back() != 'A') {
            std::cerr << "RED: expected explicit Always policy to produce a pass-visible log event\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Always);
        gentest::set_log_policy(gentest::LogPolicy::Never);
        gentest::log("explicit_never_override");
        snapshot = ctx.snapshot();
        if (snapshot.event_lines.size() != 2) {
            std::cerr << "RED: expected explicit policy to override the default log policy\n";
            return 1;
        }

        gentest::set_default_log_policy(gentest::LogPolicy::Never);
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
