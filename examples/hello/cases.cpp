#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <string>

using namespace gentest::asserts;

namespace hello {

[[using gentest: test("addition")]]
void addition() {
    const auto value = 2 + 2;
    gentest::expect_true(value == 4, "addition result");
    EXPECT_EQ(value, 4);
}

[[using gentest: test("greeting")]]
void greeting() {
    std::string message = "hello";
    message += " gentest";

    EXPECT_TRUE(message.starts_with("hello"));
    EXPECT_EQ(message, "hello gentest");
}

} // namespace hello
