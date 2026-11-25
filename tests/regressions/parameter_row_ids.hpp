#pragma once

#include "gentest/test.h"

#include <fmt/format.h>

namespace row_ids {

enum class Mode { Strict };

template <typename T>
[[using gentest: test("matrix"), template(T, int, long), parameters_pack((input, expected), ("empty", 0, 1), ("\u03BB [a];%/\\", 2, 7)),
  parameters_pack((mode), ("strict", Mode::Strict)), parameters(step, 3, 4)]]
inline void matrix(T input, int expected, Mode mode, int step) {
    gentest::expect_eq(input * 3 + 1, static_cast<T>(expected));
    gentest::expect_true(mode == Mode::Strict);
    gentest::log(fmt::format("row payload {} {} {}", static_cast<int>(input), expected, step));
}

[[using gentest: test("fails"), parameters_pack((value), ("bad input's \"id\" \u03BB", 17), ("other", 23))]] inline void fails(int value) {
    gentest::expect_eq(value, 23, "row value mismatch");
}

} // namespace row_ids
