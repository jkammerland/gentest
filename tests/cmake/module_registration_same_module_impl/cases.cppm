export module gentest.module_registration_same_module_impl;

import gentest;
import gentest.bench_util;
import gentest.module_registration_same_module_impl.support;

using namespace gentest::asserts;

export namespace module_registration_same_module_impl_ns {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = support_value(); }
    int  value = 0;
};

[[using gentest: test("module/same_module_impl")]]
void same_module_case(SuiteFixture &fixture) {
    EXPECT_EQ(fixture.value, 7);
}

[[using gentest: bench("module/same_module_impl_bench"), baseline]]
void same_module_bench(SuiteFixture &fixture) {
    gentest::doNotOptimizeAway(fixture.value);
}

[[using gentest: jitter("module/same_module_impl_jitter")]]
void same_module_jitter(SuiteFixture &fixture) {
    gentest::doNotOptimizeAway(fixture.value);
}

} // namespace module_registration_same_module_impl_ns
