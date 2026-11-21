#pragma once

#include "gentest/test.h"

#include <string>

using namespace gentest::asserts;

namespace hello {

[[gentest::test]]
inline void addition() {
    const auto value = 2 + 2;
    EXPECT_EQ(value, 4, "addition result");
}

[[gentest::test]]
inline void greeting() {
    std::string message = "hello";
    message += " gentest";

    EXPECT_TRUE(message.starts_with("hello"));
    EXPECT_EQ(message, "hello gentest");
}

} // namespace hello
