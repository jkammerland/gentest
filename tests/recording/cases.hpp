#pragma once

#include "gentest/bench_util.h"
#include "gentest/runner.h"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace rt_recording {
inline int  run_number    = 0;
inline bool break_fixture = false;

inline void payload(std::string_view name, gentest::RecordScope scope = gentest::RecordScope::Current) {
    std::array<std::byte, 3> bytes{std::byte{'a'}, std::byte{0}, std::byte{255}};
    gentest::record_data(name, bytes, "application/octet-stream", {.scope = scope, .schema = "capture/v1"});
    bytes.fill(std::byte{0});
}

struct [[gentest::fixture(global)]] RunInfo : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        gentest::record_property("run", ++run_number);
        gentest::record_property("precedence", "run");
        if (run_number == 1)
            gentest::record_property("first_run_only", true);
        payload("global");
    }
    void tearDown() override { gentest::record_property("run_teardown", true); }
};

struct [[gentest::fixture(suite)]] SuiteInfo : gentest::FixtureSetup, gentest::FixtureTearDown {
    static std::unique_ptr<SuiteInfo> gentest_allocate() {
        gentest::record_property("suite_allocation", true);
        return std::make_unique<SuiteInfo>();
    }
    void setUp() override {
        gentest::record_property("precedence", "suite");
        gentest::record_property("suite", "parent");
        payload("suite");
        if (break_fixture)
            gentest::record_property("unavailable", 1, gentest::RecordScope::Case);
    }
    void tearDown() override { gentest::record_property("suite_teardown", true); }
};

struct LocalInfo : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { gentest::record_property("local_setup", true); }
    void tearDown() override {
        gentest::record_property("local_teardown", true);
        payload("local_teardown");
    }
};

namespace ok {
[[gentest::test]] inline void scalars(LocalInfo &, SuiteInfo &, RunInfo &) {
    std::string text = "owned";
    gentest::record_property("text", std::string_view(text));
    text = "changed";
    gentest::record_property("null", nullptr);
    gentest::record_property("bool", true);
    gentest::record_property("min", std::numeric_limits<std::int64_t>::min());
    gentest::record_property("max", std::numeric_limits<std::uint64_t>::max());
    gentest::record_property("double", 1.25);
    gentest::record_property("replace", 1);
    gentest::record_property("replace", "last");
    gentest::record_property("precedence", "case");
    gentest::record_property("explicit_run", 7, gentest::RecordScope::Run);
    gentest::record_property("child_suite", "child", gentest::RecordScope::Suite);
    gentest::record_property("quotes\"<&", "Unicode: å\n\"<&");
    gentest::record_property("invalid_utf8", std::string(1, static_cast<char>(0xff)));
    gentest::record_property("nul", std::string("a\0b", 3));
    payload(std::string("invalid-") + static_cast<char>(0xff));
    payload("../same");
    payload("../same");
    gentest::record_data("empty", {}, "application/octet-stream");
}
[[gentest::test]] inline void                      separate(LocalInfo &) { gentest::record_property("only_here", 1); }
[[gentest::test]] inline gentest::async_test<void> asynchronous(LocalInfo &) {
    gentest::record_property("before", true);
    co_await gentest::async::yield();
    gentest::record_property("after", true);
    payload("async");
}
[[gentest::bench("measured")]] inline void measured(LocalInfo &) { gentest::doNotOptimizeAway(run_number); }
[[gentest::jitter("jitter")]] inline void  jitter(LocalInfo &) { gentest::doNotOptimizeAway(run_number); }
} // namespace ok

namespace bad {
[[gentest::test]] inline void invalid(LocalInfo &) {
    payload("before_error");
    gentest::record_property("", 1);
    gentest::record_property("nan", std::numeric_limits<double>::quiet_NaN());
    gentest::record_data("", {}, "application/json");
    gentest::record_data("no_mime", {}, "");
}
[[gentest::test]] inline void exception(LocalInfo &) {
    payload("before_throw");
    throw std::runtime_error("expected");
}
[[gentest::test]] inline void skip(LocalInfo &) {
    payload("before_skip");
    gentest::skip("expected");
}
[[gentest::test]] inline void xfail(LocalInfo &) {
    payload("before_xfail");
    gentest::xfail("expected");
    gentest::expect(false);
}
[[gentest::bench("timed")]] inline void    timed(LocalInfo &) { gentest::record_property("forbidden", 1); }
[[using gentest: test, death]] inline void adopted() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto lease = gentest::set_current_context(context);
        gentest::record_property("worker", 1);
    });
    worker.join();
}
} // namespace bad
namespace cancel {
inline gentest::async::manual_event                never;
[[gentest::test]] inline gentest::async_test<void> a_wait(LocalInfo &) {
    gentest::record_property("started", true);
    co_await never.wait("cancellation test");
}
[[gentest::test]] inline void b_fail() { gentest::expect(false); }
} // namespace cancel
} // namespace rt_recording
