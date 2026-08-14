#include "cases.hpp"

#include "gentest/bench_util.h"
#include "gentest_downstream_mocks.hpp"

#include <sstream>
#include <string>
#include <string_view>

using namespace gentest::asserts;

namespace {

struct RestoreDefaultLogSink {
    ~RestoreDefaultLogSink() { gentest::restore_default_log_sink(); }
};

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

namespace downstream {

void module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx) {
    EXPECT_EQ(suite_fx.value, 7);
    EXPECT_EQ(global_fx.value, 11);
}

void module_mock() {
    downstream::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

    Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

void log_sink() {
    RestoreDefaultLogSink restore;
    gentest::remove_all_log_sinks();
    std::ostringstream out;
    auto               handle = gentest::add_log_sink(gentest::make_ostream_log_sink(out));

    gentest::log("downstream meson log sink first");
    EXPECT_TRUE(contains(out.str(), "downstream meson log sink first"));
    EXPECT_TRUE(handle.remove());

    gentest::log("downstream meson log sink after remove");
    EXPECT_FALSE(contains(out.str(), "downstream meson log sink after remove"));
}

void module_bench(SuiteFixture &suite_fx) { gentest::doNotOptimizeAway(suite_fx.value); }

void module_jitter(GlobalFixture &global_fx) { gentest::doNotOptimizeAway(global_fx.value); }

} // namespace downstream
