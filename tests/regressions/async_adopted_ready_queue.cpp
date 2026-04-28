#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <atomic>
#include <future>
#include <memory>
#include <span>
#include <thread>
#include <utility>

namespace {

using namespace gentest::asserts;

gentest::async::manual_event        complete_first_case;
std::shared_ptr<std::promise<void>> worker_release;
std::atomic<bool>                   first_case_resumed{false};
std::atomic<bool>                   releaser_case_resumed{false};

void unused_sync(void *) {}

auto completed_case_with_adopted_worker() -> gentest::async_test<void> {
    complete_first_case.reset();
    worker_release = std::make_shared<std::promise<void>>();
    first_case_resumed.store(false, std::memory_order_release);
    releaser_case_resumed.store(false, std::memory_order_release);

    auto release_future = worker_release->get_future().share();
    auto context        = gentest::get_current_context();
    auto worker_context = context;
    auto started        = std::make_shared<std::promise<void>>();
    auto ready          = started->get_future();

    std::thread([context = std::move(worker_context), started = std::move(started), release_future = std::move(release_future)]() mutable {
        auto adoption = gentest::set_current_context(context);
        started->set_value();
        release_future.wait();
    }).detach();

    ready.wait();
    ASSERT_TRUE(context != nullptr);
    ASSERT_TRUE(context->adopted_contexts.load(std::memory_order_acquire) != 0);

    co_await complete_first_case.wait("later ready case did not resume first case");

    first_case_resumed.store(true, std::memory_order_release);
    EXPECT_FALSE(releaser_case_resumed.load(std::memory_order_acquire));
    EXPECT_TRUE(context->adopted_contexts.load(std::memory_order_acquire) != 0);
}

auto release_adopted_worker_after_yield() -> gentest::async_test<void> {
    complete_first_case.set();
    co_await gentest::async::yield();

    releaser_case_resumed.store(true, std::memory_order_release);
    EXPECT_TRUE(first_case_resumed.load(std::memory_order_acquire));
    ASSERT_TRUE(worker_release != nullptr);
    worker_release->set_value();
}

auto completed_case_with_adopted_worker_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(completed_case_with_adopted_worker());
}

auto release_adopted_worker_after_yield_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(release_adopted_worker_after_yield());
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/async_adopted_ready_queue/00_completed_waits_for_later_ready",
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
        .async_fn         = &completed_case_with_adopted_worker_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_ready_queue/01_releases_adopted_worker",
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
        .async_fn         = &release_adopted_worker_after_yield_fn,
        .is_async         = true,
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
