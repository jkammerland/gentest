export module gentest_hello_modules.cases;

import gentest;

using namespace gentest::asserts;

export namespace hello_modules {

[[gentest::test]]
void addition() {
    const auto value = 2 + 2;
    EXPECT_EQ(value, 4, "module addition result");
}

[[gentest::test]]
void predicate() {
    const auto predicate = [] { return true; };
    EXPECT_TRUE(predicate());
}

} // namespace hello_modules
