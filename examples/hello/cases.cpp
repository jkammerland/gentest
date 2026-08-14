#include "cases.hpp"

#include "gentest/test.h"

#include <string>

using namespace gentest::asserts;

namespace hello {

void addition() {
    const auto value = 2 + 2;
    gentest::expect_true(value == 4, "addition result");
    EXPECT_EQ(value, 4);
}

void greeting() {
    std::string message = "hello";
    message += " gentest";

    EXPECT_TRUE(message.starts_with("hello"));
    EXPECT_EQ(message, "hello gentest");
}

} // namespace hello
