#include "gentest/attributes.h"
#include "gentest/bench_util.h"
#include "gentest/context.h"
#include "gentest_downstream_mocks.hpp"
#include "private_case_value.hpp"

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

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }

    int value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }

    int value = 0;
};

[[using gentest: test("textual_test")]]
void module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx) {
    EXPECT_EQ(suite_fx.value, 7);
    EXPECT_EQ(global_fx.value, 11);
    EXPECT_EQ(private_case_value, 17);
}

[[using gentest: test("textual_mock")]]
void module_mock() {
    downstream::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

    Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

[[using gentest: test("textual_log_sink")]]
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

[[using gentest: bench("textual_bench"), baseline]]
void module_bench(SuiteFixture &suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

[[using gentest: jitter("textual_jitter")]]
void module_jitter(GlobalFixture &global_fx) {
    gentest::doNotOptimizeAway(global_fx.value);
}

#if defined(DOWNSTREAM_MESON_CODEGEN_FLAG)
[[using gentest: test("compile_flag")]]
void meson_compile_flag() {}
#else
[[using gentest: test("compile_flag_off")]]
void meson_compile_flag() {}
#endif

} // namespace downstream
