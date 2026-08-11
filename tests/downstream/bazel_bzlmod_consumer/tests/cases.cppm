module;

#include "dep_case_value.hpp"
#include "include_semantics.hpp"
#include "private_case_value.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

export module downstream.bazel.consumer_cases;

import gentest;
import gentest.bench_util;
import downstream.bazel.consumer_mocks;

using namespace gentest::asserts;

namespace {

static_assert(gentest_downstream_private_case_value == 31);
static_assert(gentest_downstream_dep_case_value == 37);
static_assert(gentest_bazel_include_semantics == 41);

struct RestoreDefaultLogSink {
    ~RestoreDefaultLogSink() { gentest::restore_default_log_sink(); }
};

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

export namespace downstream {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int  value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }
    int  value = 0;
};

[[using gentest: test("downstream/bazel/test")]]
void downstream_test(SuiteFixture &suite_fx, GlobalFixture &global_fx) {
    EXPECT_EQ(suite_fx.value, 7);
    EXPECT_EQ(global_fx.value, 11);
}

[[using gentest: test("downstream/bazel/mock")]]
void downstream_mock() {
    downstream::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

    Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

[[using gentest: test("downstream/bazel/log_sink")]]
void downstream_log_sink() {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream out;
    auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));

    gentest::log("downstream bazel log sink first");
    EXPECT_TRUE(contains(out.str(), "downstream bazel log sink first"));
    EXPECT_TRUE(handle.remove());

    gentest::log("downstream bazel log sink after remove");
    EXPECT_FALSE(contains(out.str(), "downstream bazel log sink after remove"));
}

[[using gentest: bench("downstream/bazel/bench"), baseline]]
void downstream_bench(SuiteFixture &suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

[[using gentest: jitter("downstream/bazel/jitter")]]
void downstream_jitter(GlobalFixture &global_fx) {
    gentest::doNotOptimizeAway(global_fx.value);
}

} // namespace downstream
