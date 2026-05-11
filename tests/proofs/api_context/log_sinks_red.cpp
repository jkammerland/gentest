#include "gentest/runner.h"
#include "support/context_proof_support.h"

#include <iostream>
#include <sstream>
#include <string_view>

namespace {

struct RestoreDefaultLogSink {
    ~RestoreDefaultLogSink() { gentest::restore_default_log_sink(); }
};

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

int main() {
    try {
        RestoreDefaultLogSink restore;
        gentest::remove_all_log_sinks();

        std::ostringstream first;
        std::ostringstream second;
        auto               first_handle  = gentest::add_log_sink(gentest::make_ostream_log_sink(first));
        auto               second_handle = gentest::add_log_sink(gentest::make_ostream_log_sink(second));

        gentest::test_support::ActiveProofContext ctx("proof/log_sinks_red");
        gentest::log("fanout message");
        if (!contains(first.str(), "fanout message") || !contains(second.str(), "fanout message")) {
            std::cerr << "RED: expected all registered sinks to receive log fanout\n";
            return 1;
        }

        if (!first_handle.remove() || first_handle.active()) {
            std::cerr << "RED: expected first sink handle to become inactive after removal\n";
            return 1;
        }
        gentest::log("second only");
        if (contains(first.str(), "second only") || !contains(second.str(), "second only")) {
            std::cerr << "RED: expected removed sink to stop receiving logs while remaining sink continues\n";
            return 1;
        }

        gentest::remove_all_log_sinks();
        if (second_handle.active()) {
            std::cerr << "RED: remove_all_log_sinks should make outstanding handles inactive\n";
            return 1;
        }
        gentest::log("no sinks");
        if (contains(first.str(), "no sinks") || contains(second.str(), "no sinks")) {
            std::cerr << "RED: removed sinks still received log after remove_all_log_sinks\n";
            return 1;
        }

        std::cout << "PASS: multiple log sinks and removal behave correctly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
