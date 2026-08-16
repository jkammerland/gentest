// Minimal test cases for factory registration experiment.
// This demonstrates the factory-based registration model.

#include "gentest/test.h"
#include "gentest/attributes.h"

using namespace gentest::asserts;

[[using gentest: test("factory/basic")]]
void addition_works() {
    EXPECT_EQ(2 + 2, 4);
}

[[using gentest: test("factory/string")]]
void string_concat() {
    std::string a = "hello";
    std::string b = " world";
    std::string result = a + b;
    EXPECT_EQ(result, "hello world");
}
