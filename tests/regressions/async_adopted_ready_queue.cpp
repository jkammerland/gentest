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
gentest::async::manual_event        never_resume_after_failure;
gentest::async::manual_event        unrelated_adopted_finish;
std::shared_ptr<std::promise<void>> worker_release;
std::atomic<bool>                   first_case_resumed{false};
std::atomic<bool>                   releaser_case_resumed{false};
std::atomic<bool>                   failure_worker_started{false};
std::atomic<bool>                   isolated_failure_recorded{false};
std::atomic<bool>                   isolated_neighbor_started{false};

void unused_sync(void *) {}

auto completed_case_with_adopted_worker() -> gentest::async_test<void> {
    gentest::set_log_policy(gentest::LogPolicy::Always);

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

auto completed_case_with_adopted_failure_worker() -> gentest::async_test<void> {
    failure_worker_started.store(false, std::memory_order_release);

    co_await gentest::async::yield();

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        failure_worker_started.store(true, std::memory_order_release);
        started->set_value();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        EXPECT_TRUE(false, "adopted worker failure should wake fail-fast adopted drain");

        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();

    ready.wait();
    co_return;
}

auto completed_case_with_adopted_failure_worker_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(completed_case_with_adopted_failure_worker());
}

auto suspended_case_with_adopted_failure_worker() -> gentest::async_test<void> {
    never_resume_after_failure.reset_all();

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();

        EXPECT_TRUE(false, "adopted worker failure while owner remains suspended");

        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();

    ready.wait();
    co_await never_resume_after_failure.wait("owner remains suspended after adopted failure");
}

auto suspended_case_with_adopted_failure_worker_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(suspended_case_with_adopted_failure_worker());
}

auto suspended_xfail_case_with_adopted_failure_worker() -> gentest::async_test<void> {
    never_resume_after_failure.reset_all();
    gentest::xfail("expected adopted worker failure");

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();

        EXPECT_TRUE(false, "xfail adopted worker failure should wake adopted drain");

        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();

    ready.wait();
    co_await never_resume_after_failure.wait("xfail owner remains suspended after adopted failure");
}

auto suspended_xfail_case_with_adopted_failure_worker_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(suspended_xfail_case_with_adopted_failure_worker());
}

auto isolated_failed_adopted_worker() -> gentest::async_test<void> {
    never_resume_after_failure.reset_all();
    unrelated_adopted_finish.reset_all();
    isolated_failure_recorded.store(false, std::memory_order_release);
    isolated_neighbor_started.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();

        while (!isolated_neighbor_started.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        EXPECT_TRUE(false, "isolated adopted worker failure");
        isolated_failure_recorded.store(true, std::memory_order_release);

        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();

    ready.wait();
    co_await never_resume_after_failure.wait("failed adopted worker owner remains suspended");
}

auto isolated_failed_adopted_worker_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(isolated_failed_adopted_worker());
}

auto unrelated_adopted_worker_finishes_after_neighbor_failure() -> gentest::async_test<void> {
    gentest::set_log_policy(gentest::LogPolicy::Always);

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        isolated_neighbor_started.store(true, std::memory_order_release);
        started->set_value();

        while (!isolated_failure_recorded.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        unrelated_adopted_finish.set("isolated.neighbor.ready");
    }).detach();

    ready.wait();
    co_await unrelated_adopted_finish.wait("isolated.neighbor.ready");
    EXPECT_FALSE(context.stop_requested(), "unrelated adopted context should not be stopped by neighbor failure");
    gentest::log("unrelated adopted case completed naturally");
}

auto unrelated_adopted_worker_finishes_after_neighbor_failure_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(unrelated_adopted_worker_finishes_after_neighbor_failure());
}

struct StopConditionState {
    std::mutex                  mtx;
    std::condition_variable_any cv_any;
    std::condition_variable     cv;
    bool                        ready = false;
};

auto condition_variable_any_worker_observes_stop() -> gentest::async_test<void> {
    gentest::set_log_policy(gentest::LogPolicy::Always);

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
        EXPECT_FALSE(completed, "condition_variable_any wait should stop before predicate becomes ready");
        EXPECT_TRUE(stop.stop_requested(), "condition_variable_any worker should observe context stop");
        gentest::log("condition_variable_any worker observed context stop");
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
        EXPECT_FALSE(state->ready, "plain condition_variable wait should stop before predicate becomes ready");
        EXPECT_TRUE(stop.stop_requested(), "plain condition_variable worker should observe context stop");
    }).detach();

    ready.wait();
    co_return;
}

auto condition_variable_worker_observes_stop_fn(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(condition_variable_worker_observes_stop());
}

auto stop_callback_can_use_gentest_context() -> gentest::async_test<void> {
    gentest::set_log_policy(gentest::LogPolicy::Always);

    auto context = gentest::get_current_context();
    ASSERT_TRUE(static_cast<bool>(context));

    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        auto stop  = context.stop_token();

        std::stop_callback log_on_stop(stop, [] { gentest::log("stop callback observed active gentest context"); });

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
        .name             = "regressions/async_adopted_failure_wake/00_completed_adopted_worker_fails",
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
        .async_fn         = &completed_case_with_adopted_failure_worker_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_failure_wake/01_suspended_adopted_worker_fails",
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
        .async_fn         = &suspended_case_with_adopted_failure_worker_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_xfail_failure_wake/00_suspended_adopted_worker_xfails",
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
        .async_fn         = &suspended_xfail_case_with_adopted_failure_worker_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_failure_isolation/00_failed_adopted_owner_suspended",
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
        .async_fn         = &isolated_failed_adopted_worker_fn,
        .is_async         = true,
    },
    {
        .name             = "regressions/async_adopted_failure_isolation/01_unrelated_adopted_finishes",
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
        .async_fn         = &unrelated_adopted_worker_finishes_after_neighbor_failure_fn,
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
