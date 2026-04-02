#include "gentest/runner.h"

#include <iostream>
#include <source_location>
#include <string_view>

int main() {
    try {
        bool expect_throw_failure_rethrew    = false;
        bool require_throw_failure_rethrew   = false;
        bool expect_throw_assertion_rethrew  = false;
        bool require_throw_assertion_rethrew = false;

        try {
            gentest::detail::expect_throw<std::exception>([] { throw gentest::failure("framework failure passthrough"); }, "std::exception",
                                                          std::source_location::current());
        } catch (const gentest::failure &) { expect_throw_failure_rethrew = true; }

        try {
            gentest::detail::require_throw<std::exception>([] { throw gentest::failure("framework failure passthrough"); },
                                                           "std::exception", std::source_location::current());
        } catch (const gentest::failure &) { require_throw_failure_rethrew = true; }

        try {
            gentest::detail::expect_throw<std::exception>([] { throw gentest::assertion("framework assertion passthrough"); },
                                                          "std::exception", std::source_location::current());
        } catch (const gentest::assertion &) { expect_throw_assertion_rethrew = true; }

        try {
            gentest::detail::require_throw<std::exception>([] { throw gentest::assertion("framework assertion passthrough"); },
                                                           "std::exception", std::source_location::current());
        } catch (const gentest::assertion &) { require_throw_assertion_rethrew = true; }

        if (expect_throw_failure_rethrew && require_throw_failure_rethrew && expect_throw_assertion_rethrew &&
            require_throw_assertion_rethrew) {
            std::cout << "PASS: framework exceptions are rethrown even when Expected=std::exception\n";
            return 0;
        }

        std::cerr << "FAIL: expected framework exceptions to be rethrown through throw helpers\n";
        std::cerr << "expect_throw_failure_rethrew=" << expect_throw_failure_rethrew
                  << ", require_throw_failure_rethrew=" << require_throw_failure_rethrew << '\n';
        std::cerr << "expect_throw_assertion_rethrew=" << expect_throw_assertion_rethrew
                  << ", require_throw_assertion_rethrew=" << require_throw_assertion_rethrew << '\n';
    } catch (const std::exception &e) {
        std::cerr << "FAIL: unexpected std::exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected non-std exception\n";
        return 1;
    }
    return 1;
}
