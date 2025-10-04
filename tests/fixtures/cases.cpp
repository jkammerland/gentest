#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "gentest/fixture.h"

#include <vector>

namespace fixtures::ephemeral {

struct StackFixture {
    std::vector<int> data;

    [[using gentest: test("fixtures/ephemeral/size_zero")]]
    void size_zero() {
        gentest::expect_eq(data.size(), std::size_t{0}, "fresh instance has size 0");
    }

    [[using gentest: test("fixtures/ephemeral/push_pop")]]
    void push_pop() {
        data.push_back(1);
        gentest::expect_eq(data.back(), 1, "push stores value");
        data.pop_back();
        gentest::expect_eq(data.size(), std::size_t{0}, "pop restores size");
    }
};

} // namespace fixtures::ephemeral

namespace fixtures::stateful {

struct [[using gentest: stateful_fixture]] Counter /* optionally implement setup/teardown later */ {
    int x = 0;

    [[using gentest: test("fixtures/stateful/a_set_flag")]]
    void set_flag() {
        x = 1;
    }

    [[using gentest: test("fixtures/stateful/b_check_flag")]]
    void check_flag() {
        gentest::expect_eq(x, 1, "state preserved across methods");
    }
};

} // namespace fixtures::stateful

// Free-function fixtures composed via attribute
namespace fixtures::free_compose {

struct A : gentest::FixtureSetup, gentest::FixtureTearDown {
    int phase = 0;
    void setUp() override {
        gentest::expect_eq(phase, 0, "A::setUp before test");
        phase = 1;
    }
    void tearDown() override {
        gentest::expect_eq(phase, 2, "A::tearDown after test");
        phase = 3;
    }
};

struct B { const char* msg = "ok"; };
class C { public: int v = 7; };

[[using gentest: test("fixtures/free/basic"), fixtures(A, B, C)]]
constexpr void free_basic(A& a, B& b, C& c) {
    // setUp must have run for A
    gentest::expect_eq(a.phase, 1, "A setUp ran");
    a.phase = 2; // allow tearDown to validate
    gentest::expect(std::string(b.msg) == "ok", "B default value");
    gentest::expect_eq(c.v, 7, "C default value");
}

} // namespace fixtures::free_compose
