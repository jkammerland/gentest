#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

namespace fx {

struct CounterBase : gentest::FixtureSetup, gentest::FixtureTearDown {
    int set_up_calls = 0;
    int touches      = 0;

    void setUp() override { ++set_up_calls; }

    void touch() { ++touches; }

    void tearDown() override {
        EXPECT_EQ(set_up_calls, 1, "fixture setup runs once");
        EXPECT_EQ(touches, 1, "local fixture is touched once");
    }
};

template <int ExpectedTouches> struct SharedCounterBase : CounterBase {
    void tearDown() override {
        EXPECT_EQ(this->set_up_calls, 1, "shared fixture setup runs once");
        EXPECT_EQ(this->touches, ExpectedTouches, "shared fixture collected all touches");
    }
};

// Declare the global fixture in the common ancestor namespace so it is visible
// to both `fx::local` and `fx::shared`.
struct [[gentest::fixture(global)]] GlobalCounter : SharedCounterBase<3> {};

namespace local {

struct LocalCounter : CounterBase {};

[[gentest::test]]
void one(LocalCounter &local_fx, GlobalCounter &global_fx) {
    local_fx.touch();
    global_fx.touch();
}

} // namespace local

namespace shared {

struct [[gentest::fixture(suite)]] SuiteCounter : SharedCounterBase<2> {};

[[gentest::test]]
void first(SuiteCounter &suite_fx, GlobalCounter &global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

[[gentest::test]]
void second(SuiteCounter &suite_fx, GlobalCounter &global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

} // namespace shared

} // namespace fx
