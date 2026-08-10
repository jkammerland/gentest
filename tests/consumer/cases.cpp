#include "bazel_dep_case_value.hpp"
#include "bazel_private_case_value.hpp"
#include "gentest/attributes.h"
#include "gentest/bench_util.h"
#include "gentest/context.h"
#include "gentest_consumer_mocks.hpp"

#include <sstream>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

using namespace gentest::asserts;

namespace {

static_assert(sizeof(size_t) > 0);
static_assert(sizeof(std::vector<int>) > 0);
static_assert(gentest_bazel_private_case_value == 23);
static_assert(gentest_bazel_dep_case_value == 29);

struct RestoreDefaultLogSink {
    ~RestoreDefaultLogSink() { gentest::restore_default_log_sink(); }
};

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

namespace consumer {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }

    int value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }

    int value = 0;
};

[[using gentest: test("consumer/module_test")]]
void module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx) {
    EXPECT_EQ(suite_fx.value, 7);
    EXPECT_EQ(global_fx.value, 11);
}

#if defined(GENTEST_BAZEL_MOCK_PRIVATE_DEFINE)
[[using gentest: test("consumer/mock_private_define_must_not_leak")]]
void mock_private_define_must_not_leak() {}
#endif

[[using gentest: test("consumer/module_mock")]]
void module_mock() {
    consumer::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

    Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

[[using gentest: test("consumer/log_sink")]]
void log_sink() {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream out;
    auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));

    gentest::log("consumer log sink first");
    EXPECT_TRUE(contains(out.str(), "consumer log sink first"));
    EXPECT_TRUE(handle.remove());

    gentest::log("consumer log sink after remove");
    EXPECT_FALSE(contains(out.str(), "consumer log sink after remove"));
}

[[using gentest: bench("consumer/module_bench"), baseline]]
void module_bench(SuiteFixture &suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

[[using gentest: jitter("consumer/module_jitter")]]
void module_jitter(GlobalFixture &global_fx) {
    gentest::doNotOptimizeAway(global_fx.value);
}

} // namespace consumer
