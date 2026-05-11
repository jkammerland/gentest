#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"

#include <span>
#include <sstream>
#include <string>
#include <thread>

namespace {

using namespace gentest::asserts;

void unused_sync(void *) {}

struct RestoreDefaultLogSink {
    ~RestoreDefaultLogSink() { gentest::restore_default_log_sink(); }
};

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

void default_sink_visible_on_pass(void *) { gentest::log("default sink visible on pass"); }

void default_sink_visible_on_sync_fail(void *) {
    gentest::log("default sink visible on sync fail");
    EXPECT_TRUE(false, "sync failure does not replay streamed logs");
}

void custom_sink_receives_logs(void *) {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream out;
    auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));
    gentest::log("custom sink first");
    EXPECT_TRUE(contains(out.str(), "custom sink first"));
    EXPECT_TRUE(handle.remove());
    gentest::log("custom sink after removal");
    EXPECT_FALSE(contains(out.str(), "custom sink after removal"));
}

void handle_destruction_does_not_remove_sink(void *) {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream out;
    {
        auto handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));
        EXPECT_TRUE(handle.active());
    }

    gentest::log("sink survives handle destruction");

    EXPECT_TRUE(contains(out.str(), "sink survives handle destruction"));
}

void explicit_remove_before_reassigning_handle(void *) {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream first;
    std::ostringstream second;
    auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(first));
    gentest::log("first sink before explicit remove");
    EXPECT_TRUE(contains(first.str(), "first sink before explicit remove"));
    EXPECT_TRUE(handle.remove());

    handle = gentest::add_log_sink(gentest::make_ostream_log_sink(second));
    gentest::log("second sink after explicit remove");

    EXPECT_FALSE(contains(first.str(), "second sink after explicit remove"));
    EXPECT_TRUE(contains(second.str(), "second sink after explicit remove"));
}

void remove_all_sinks_silences_default(void *) {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    gentest::log("removed sinks hidden on pass");
}

gentest::async_test<void> async_default_sink_visible_on_fail_impl() {
    gentest::log("default sink visible on async fail");
    co_await gentest::async::yield();
    EXPECT_TRUE(false, "async failure does not replay streamed logs");
}

auto async_default_sink_visible_on_fail(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(async_default_sink_visible_on_fail_impl());
}

gentest::async_test<void> async_default_sink_visible_on_pass_impl() {
    gentest::log("default sink visible on async pass");
    co_await gentest::async::yield();
}

auto async_default_sink_visible_on_pass(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(async_default_sink_visible_on_pass_impl());
}

void adopted_thread_log_visible(void *) {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto guard = gentest::set_current_context(context);
        gentest::log("default sink visible from adopted thread");
    });
    worker.join();
}

void unadopted_thread_log_crashes(void *) {
    std::thread worker([] { gentest::log("this unadopted log must crash"); });
    worker.join();
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/logging_output/default_sink_visible_on_pass",
        .fn               = &default_sink_visible_on_pass,
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
        .name             = "regressions/logging_output/default_sink_visible_on_sync_fail",
        .fn               = &default_sink_visible_on_sync_fail,
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
        .name             = "regressions/logging_output/custom_sink_receives_logs",
        .fn               = &custom_sink_receives_logs,
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
        .name             = "regressions/logging_output/remove_all_sinks_silences_default",
        .fn               = &remove_all_sinks_silences_default,
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
        .name             = "regressions/logging_output/handle_destruction_does_not_remove_sink",
        .fn               = &handle_destruction_does_not_remove_sink,
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
        .name             = "regressions/logging_output/explicit_remove_before_reassigning_handle",
        .fn               = &explicit_remove_before_reassigning_handle,
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
        .name             = "regressions/logging_output/async_default_sink_visible_on_fail",
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
        .async_fn         = &async_default_sink_visible_on_fail,
        .is_async         = true,
    },
    {
        .name             = "regressions/logging_output/async_default_sink_visible_on_pass",
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
        .async_fn         = &async_default_sink_visible_on_pass,
        .is_async         = true,
    },
    {
        .name             = "regressions/logging_output/adopted_thread_log_visible",
        .fn               = &adopted_thread_log_visible,
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
