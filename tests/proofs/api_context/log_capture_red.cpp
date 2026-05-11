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
        std::ostringstream out;
        auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));

        gentest::test_support::ActiveProofContext ctx("proof/log_sink_capture_red");
        gentest::log("captured and streamed");
        const auto snapshot = ctx.snapshot();
        if (snapshot.logs.size() != 1 || snapshot.logs.front() != "captured and streamed") {
            std::cerr << "RED: expected log message to remain in logs payload\n";
            return 1;
        }
        if (!snapshot.event_lines.empty() || !snapshot.event_kinds.empty()) {
            std::cerr << "RED: expected logs to stay out of the failure event timeline\n";
            return 1;
        }
        if (!contains(out.str(), "captured and streamed")) {
            std::cerr << "RED: expected registered sink to receive log immediately\n";
            return 1;
        }

        if (!handle.remove()) {
            std::cerr << "RED: expected sink handle removal to report success\n";
            return 1;
        }
        gentest::log("after removal");
        if (contains(out.str(), "after removal")) {
            std::cerr << "RED: removed sink still received log\n";
            return 1;
        }

        std::cout << "PASS: log capture and sink streaming behave correctly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RED: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "RED: unexpected non-std exception\n";
        return 1;
    }
}
