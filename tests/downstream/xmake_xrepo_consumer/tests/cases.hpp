#pragma once

#include "gentest/attributes.h"
#include "gentest/fixture.h"

namespace downstream {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int  value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }
    int  value = 0;
};

[[using gentest: test("downstream/xrepo/test")]] void             downstream_test(SuiteFixture &suite_fx, GlobalFixture &global_fx);
[[using gentest: test("downstream/xrepo/mock")]] void             downstream_mock();
[[using gentest: test("downstream/xrepo/log_sink")]] void         downstream_log_sink();
[[using gentest: bench("downstream/xrepo/bench"), baseline]] void downstream_bench(SuiteFixture &suite_fx);
[[using gentest: jitter("downstream/xrepo/jitter")]] void         downstream_jitter(GlobalFixture &global_fx);

} // namespace downstream
