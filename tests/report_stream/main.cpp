#include "cases.hpp"

#include <cstdio>
#include <sstream>
#include <string_view>
#include <vector>

int main(int argc, char **argv) {
    // Preserve the runner's normal discovery interface for this authored suite.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]).starts_with("--list") || std::string_view(argv[i]) == "--help") {
            return gentest::run_all_tests(argc, argv);
        }
    }

    // An application-owned sink must still receive every message, even when
    // the default sink is disabled or routed away from the machine report.
    std::ostringstream        captured;
    auto                      sink = gentest::add_log_sink(gentest::make_ostream_log_sink(captured));
    std::vector<const char *> args;
    bool                      restore_probe = false;
    for (int i = 0; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--without-default-sink") {
            gentest::remove_all_log_sinks();
            (void)sink.remove();
            sink = gentest::add_log_sink(gentest::make_ostream_log_sink(captured));
        } else if (std::string_view(argv[i]) == "--verify-restoration") {
            restore_probe = true;
        } else {
            args.push_back(argv[i]);
        }
    }

    const int  result = gentest::run_all_tests(args);
    const auto logs   = captured.str();
    (void)sink.remove();
    for (auto marker :
         {"shared setup marker", "local setup marker", "body marker", "worker marker", "local teardown marker", "shared teardown marker"}) {
        if (logs.find(marker) == std::string::npos) {
            (void)std::fputs("custom sink lost a lifecycle message\n", stderr);
            return 86;
        }
    }

    // A subsequent ordinary invocation in the same process must log to stdout.
    if (restore_probe) {
        const char *next[]{"--run=report_stream/ordinary", "--no-color"};
        return gentest::run_all_tests(next);
    }
    return result;
}
