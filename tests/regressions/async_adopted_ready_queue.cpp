#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
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
    complete_first_case.reset_all();
    worker_release = std::make_shared<std::promise<void>>();
    first_case_resumed.store(false, std::memory_order_release);
    releaser_case_resumed.store(false, std::memory_order_release);

    auto release_future = worker_release->get_future().share();
    auto context        = gentest::get_current_context();
    auto worker_context = context;
    auto started        = std::make_shared<std::promise<void>>();
    auto ready          = started->get_future();

    std::thread([context = std::move(worker_context), started = std::move(started), release_future = std::move(release_future)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();
        release_future.wait();
        gentest::log("adopted worker released after owner completion");
    }).detach();

    ready.wait();
    ASSERT_TRUE(static_cast<bool>(context));

    co_await complete_first_case.wait("later ready case released first adopted case");

    first_case_resumed.store(true, std::memory_order_release);
    EXPECT_FALSE(releaser_case_resumed.load(std::memory_order_acquire));
}

auto release_adopted_worker_after_yield() -> gentest::async_test<void> {
    complete_first_case.set("later ready case released first adopted case");
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

struct StopConditionState {
    std::mutex                  mtx;
    std::condition_variable_any cv_any;
    std::condition_variable     cv;
    bool                        ready = false;
};

auto condition_variable_any_worker_observes_stop() -> gentest::async_test<void> {
    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto state   = std::make_shared<StopConditionState>();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), state = std::move(state), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        auto stop  = context.stop_token();

        started->set_value();
        std::unique_lock lk(state->mtx);
        const bool       completed = state->cv_any.wait(lk, stop, [&] { return state->ready; });
        if (!completed && stop.stop_requested()) {
            gentest::log("condition_variable_any worker observed context stop");
        }
    }).detach();

    ready.wait();
    co_return;
}

auto condition_variable_any_worker_observes_stop_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(condition_variable_any_worker_observes_stop());
}

auto condition_variable_worker_observes_stop() -> gentest::async_test<void> {
    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto state   = std::make_shared<StopConditionState>();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), state = std::move(state), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        auto stop  = context.stop_token();

        std::stop_callback wake_on_stop(stop, [&] { state->cv.notify_all(); });

        started->set_value();
        std::unique_lock lk(state->mtx);
        state->cv.wait(lk, [&] { return state->ready || stop.stop_requested(); });
        if (!state->ready && stop.stop_requested()) {
            gentest::log("condition_variable worker observed context stop");
        }
    }).detach();

    ready.wait();
    co_return;
}

auto condition_variable_worker_observes_stop_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(condition_variable_worker_observes_stop());
}

auto stop_callback_can_use_gentest_context() -> gentest::async_test<void> {
    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        auto stop  = context.stop_token();

        std::stop_callback log_on_stop(stop, [context] {
            auto callback_lease = gentest::set_current_context(context);
            gentest::log("stop callback observed leased gentest context");
        });

        started->set_value();
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();

    ready.wait();
    co_return;
}

auto stop_callback_can_use_gentest_context_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(stop_callback_can_use_gentest_context());
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
    {
        .name             = "regressions/async_adopted_stop_token/00_condition_variable_any",
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
        .async_fn         = &condition_variable_any_worker_observes_stop_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_stop_token/01_condition_variable",
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
        .async_fn         = &condition_variable_worker_observes_stop_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_stop_token/02_stop_callback_can_log",
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
        .async_fn         = &stop_callback_can_use_gentest_context_fn,
        .is_async         = true,
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
