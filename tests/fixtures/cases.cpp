#include "gentest/attributes.h"
#include "gentest/runner.h"

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
