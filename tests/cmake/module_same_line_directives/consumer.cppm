module;

export module gentest.same_line_consumer;
import gentest.same_line_provider;

import gentest;

using namespace gentest::asserts;

export namespace same_line {

[[using gentest: test("same_line/basic")]]
void basic() {
    EXPECT_EQ(same_line::provider::value(), 42);
}

} // namespace same_line
