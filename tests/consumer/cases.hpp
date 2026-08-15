#pragma once

#include "gentest/attributes.h"
#include "gentest/fixture.h"

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
void module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx);

#if defined(GENTEST_BAZEL_MOCK_PRIVATE_DEFINE)
[[using gentest: test("consumer/mock_private_define_must_not_leak")]]
void mock_private_define_must_not_leak();
#endif

[[using gentest: test("consumer/module_mock")]]
void module_mock();

[[using gentest: test("consumer/log_sink")]]
void log_sink();

[[using gentest: bench("consumer/module_bench"), baseline]]
void module_bench(SuiteFixture &suite_fx);

[[using gentest: jitter("consumer/module_jitter")]]
void module_jitter(GlobalFixture &global_fx);

} // namespace consumer
