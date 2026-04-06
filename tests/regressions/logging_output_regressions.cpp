#include "gentest/runner.h"

#include <span>

namespace {

void on_failure_policy_pass_silent(void *) {
    gentest::set_log_policy(gentest::LogPolicy::OnFailure);
    gentest::log("failure-only hidden on pass");
}

void always_policy_visible_on_pass(void *) {
    gentest::set_log_policy(gentest::LogPolicy::Always);
    gentest::log("always-policy visible on pass");
}

void default_always_policy_visible_on_pass(void *) {
    gentest::set_default_log_policy(gentest::LogPolicy::Always);
    gentest::log("default-always visible on pass");
    gentest::set_default_log_policy(gentest::LogPolicy::Never);
}

void explicit_never_overrides_default_always(void *) {
    gentest::set_default_log_policy(gentest::LogPolicy::Always);
    gentest::set_log_policy(gentest::LogPolicy::Never);
    gentest::log("default-always overridden by explicit never");
    gentest::set_default_log_policy(gentest::LogPolicy::Never);
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/logging_output/on_failure_policy_pass_silent",
        .fn               = &on_failure_policy_pass_silent,
        .file             = __FILE__,
        .line             = __LINE__,
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
        .name             = "regressions/logging_output/always_policy_visible_on_pass",
        .fn               = &always_policy_visible_on_pass,
        .file             = __FILE__,
        .line             = __LINE__,
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
        .name             = "regressions/logging_output/default_always_policy_visible_on_pass",
        .fn               = &default_always_policy_visible_on_pass,
        .file             = __FILE__,
        .line             = __LINE__,
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
        .name             = "regressions/logging_output/explicit_never_overrides_default_always",
        .fn               = &explicit_never_overrides_default_always,
        .file             = __FILE__,
        .line             = __LINE__,
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
