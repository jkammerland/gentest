export module gentest_hello_modules.cases;

import gentest;

using namespace gentest::asserts;

export namespace hello_modules {

[[using gentest: test("addition")]]
void addition() {
    const auto value = 2 + 2;
    gentest::expect_true(value == 4, "module addition result");
    EXPECT_EQ(value, 4);
}

[[using gentest: test("predicate")]]
void predicate() {
    const auto predicate = [] { return true; };
    gentest::expect_true(predicate());
    EXPECT_TRUE(predicate());
}

} // namespace hello_modules
