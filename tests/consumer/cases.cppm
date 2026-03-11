module;

#include <memory>

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#include "gentest/bench_util.h"
#endif

export module gentest.consumer_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.bench_util;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace consumer {

struct Service {
    virtual ~Service()            = default;
    virtual int compute(int arg) = 0;
};

} // namespace consumer

export namespace consumer {

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

[[using gentest: test("consumer/module_mock")]]
void module_mock() {
    gentest::mock<Service> mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

#if !defined(GENTEST_CODEGEN)
    Service &service = mock_service;
    EXPECT_EQ(service.compute(3), 9);
#endif
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
