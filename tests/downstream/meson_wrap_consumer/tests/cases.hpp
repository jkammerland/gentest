#pragma once

#include "gentest/attributes.h"
#include "gentest/fixture.h"

namespace downstream {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }

    int value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }

    int value = 0;
};

[[using gentest: test("textual_test")]] void             module_test(SuiteFixture &suite_fx, GlobalFixture &global_fx);
[[using gentest: test("textual_mock")]] void             module_mock();
[[using gentest: test("textual_log_sink")]] void         log_sink();
[[using gentest: bench("textual_bench"), baseline]] void module_bench(SuiteFixture &suite_fx);
[[using gentest: jitter("textual_jitter")]] void         module_jitter(GlobalFixture &global_fx);

} // namespace downstream
