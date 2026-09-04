#include "cases.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv) {
    std::vector<const char *> args;
    bool                      twice = false;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--record-twice") == 0)
            twice = true;
        else if (std::strcmp(argv[i], "--record-fixture-error") == 0)
            rt_recording::break_fixture = true;
        else if (std::strcmp(argv[i], "--record-outside") == 0)
            gentest::record_property("outside", 1);
        else
            args.push_back(argv[i]);
    }
    const int first = gentest::run_all_tests(args);
    return twice ? (gentest::run_all_tests(args) || first) : first;
}
