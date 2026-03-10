#include "gentest/runner.h"

#include <atomic>
#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::RetryAfterFailureFixture";

std::atomic<int> g_create_attempts{0};
std::atomic<int> g_case_runs{0};

std::shared_ptr<void> create_fixture(std::string_view, std::string &error) {
    const int attempt = g_create_attempts.fetch_add(1, std::memory_order_relaxed);
    if (attempt == 0) {
        error = "transient-create-failure";
        return {};
    }
    return std::make_shared<int>(42);
}

void smoke_case(void *) { g_case_runs.fetch_add(1, std::memory_order_relaxed); }

auto run_selected_case() -> int {
    const char *args[] = {
        "gentest",
        "--run=regressions/shared_fixture_retry_after_failure/smoke",
        "--kind=test",
    };
    return gentest::run_all_tests(std::span<const char *>(args, 3));
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_retry_after_failure/smoke",
        .fn               = &smoke_case,
        .file             = __FILE__,
        .line             = 22,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kFixtureName,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions",
    },
};

} // namespace

int main() {
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));

    const int first_rc = run_selected_case();
    if (first_rc != 1)
        return 1;
    if (g_case_runs.load(std::memory_order_relaxed) != 0)
        return 2;

    const int second_rc = run_selected_case();
    if (second_rc != 0)
        return 3;
    if (g_create_attempts.load(std::memory_order_relaxed) != 2)
        return 4;
    if (g_case_runs.load(std::memory_order_relaxed) != 1)
        return 5;

    return 0;
}
