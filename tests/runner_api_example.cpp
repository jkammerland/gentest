#include "gentest/runner.h"

#include <cstddef>
#include <span>
#include <vector>

void check_service(void *) {
    // The Gentest assertion APIs are available while this callable runs.
}

static const gentest::Case cases[] = {{
    .name             = "embedded/check_service",
    .fn               = &check_service,
    .file             = __FILE__,
    .line             = __LINE__,
    .fixture_lifetime = gentest::FixtureLifetime::None,
    .suite            = "embedded",
}};

int main(int argc, char **argv) {
    std::vector<const char *> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    return gentest::run_cases(cases, args);
}
