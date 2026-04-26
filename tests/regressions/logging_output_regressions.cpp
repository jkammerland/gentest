#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"

#include <span>
#include <thread>

namespace {

using namespace gentest::asserts;

void unused_sync(void *) {}

void on_failure_policy_pass_silent(void *) {
    gentest::set_log_policy(gentest::LogPolicy::OnFailure);
    gentest::log("failure-only hidden on pass");
}

void on_failure_policy_fail_visible(void *) {
    gentest::set_log_policy(gentest::LogPolicy::OnFailure);
    gentest::log("failure-only visible on sync fail");
    EXPECT_TRUE(false, "sync failure shows failure-only logs");
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

gentest::async_test<void> async_on_failure_policy_fail_visible_impl() {
    gentest::set_log_policy(gentest::LogPolicy::OnFailure);
    gentest::log("failure-only visible on async fail");
    co_await gentest::async::yield();
    EXPECT_TRUE(false, "async failure shows failure-only logs");
}

auto async_on_failure_policy_fail_visible(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(async_on_failure_policy_fail_visible_impl());
}

gentest::async_test<void> async_always_policy_visible_on_pass_impl() {
    gentest::set_log_policy(gentest::LogPolicy::Always);
    gentest::log("always-policy visible on async pass");
    co_await gentest::async::yield();
}

auto async_always_policy_visible_on_pass(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(async_always_policy_visible_on_pass_impl());
}

void adopted_thread_on_failure_log_visible(void *) {
    gentest::set_log_policy(gentest::LogPolicy::OnFailure);
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto guard = gentest::set_current_context(context);
        gentest::log("failure-only visible from adopted thread");
    });
    worker.join();
    EXPECT_TRUE(false, "parent failure shows adopted child log");
}

void unadopted_thread_log_crashes(void *) {
    std::thread worker([] { gentest::log("this unadopted log must crash"); });
    worker.join();
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
        .name             = "regressions/logging_output/on_failure_policy_fail_visible",
        .fn               = &on_failure_policy_fail_visible,
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
    {
        .name             = "regressions/logging_output/async_on_failure_policy_fail_visible",
        .fn               = &unused_sync,
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
        .async_fn         = &async_on_failure_policy_fail_visible,
        .is_async         = true,
    },
    {
        .name             = "regressions/logging_output/async_always_policy_visible_on_pass",
        .fn               = &unused_sync,
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
        .async_fn         = &async_always_policy_visible_on_pass,
        .is_async         = true,
    },
    {
        .name             = "regressions/logging_output/adopted_thread_on_failure_log_visible",
        .fn               = &adopted_thread_on_failure_log_visible,
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
        .name             = "regressions/logging_output/unadopted_thread_log_crashes",
        .fn               = &unadopted_thread_log_crashes,
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
