#include "gentest/runner.h"

namespace {

void smoke(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/ambiguity/alpha/common_leaf",
        .fn               = &smoke,
        .file             = __FILE__,
        .line             = 7,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/ambiguity/beta/common_leaf",
        .fn               = &smoke,
        .file             = __FILE__,
        .line             = 8,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
