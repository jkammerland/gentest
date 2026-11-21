#include "cases.hpp"

#include "gentest/test.h"

#include <string>

using namespace gentest::asserts;

namespace hello {

void addition() {
    const auto value = 2 + 2;
    EXPECT_EQ(value, 4, "addition result");
}

void greeting() {
    std::string message = "hello";
    message += " gentest";

    EXPECT_TRUE(message.starts_with("hello"));
    EXPECT_EQ(message, "hello gentest");
}

} // namespace hello
