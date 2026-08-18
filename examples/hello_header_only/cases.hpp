#pragma once

#include "gentest/test.h"

#include <string>

namespace hello {

[[using gentest: test("addition")]]
inline void addition() {
    const auto value = 2 + 2;
    gentest::expect_true(value == 4, "addition result");
    gentest::asserts::EXPECT_EQ(value, 4);
}

[[using gentest: test("greeting")]]
inline void greeting() {
    std::string message = "hello";
    message += " gentest";

    gentest::asserts::EXPECT_TRUE(message.starts_with("hello"));
    gentest::asserts::EXPECT_EQ(message, "hello gentest");
}

} // namespace hello
