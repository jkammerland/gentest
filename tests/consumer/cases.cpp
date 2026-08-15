#include "cases.hpp"

#include "bazel_dep_case_value.hpp"
#include "bazel_private_case_value.hpp"
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

void module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx) {
    EXPECT_EQ(suite_fx.value, 7);
    EXPECT_EQ(global_fx.value, 11);
}

#if defined(GENTEST_BAZEL_MOCK_PRIVATE_DEFINE)
void mock_private_define_must_not_leak() {}
#endif

void module_mock() {
    consumer::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

    Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

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

void module_bench(SuiteFixture &suite_fx) { gentest::doNotOptimizeAway(suite_fx.value); }

void module_jitter(GlobalFixture &global_fx) { gentest::doNotOptimizeAway(global_fx.value); }

} // namespace consumer
