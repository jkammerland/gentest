#pragma once

#include "gentest/test.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

namespace parameterized {

// Independent axes produce 3 x 3 cases, including empty containers and zero values.
[[using gentest: test("filled_vector"), parameters(count, 0, 1, 4), parameters(value, -1, 0, 1)]]
inline void filledVector(int count, int value) {
    const std::vector<int> values(static_cast<std::size_t>(count), value);
    gentest::expect_eq(values.size(), static_cast<std::size_t>(count));
    gentest::expect_true(std::all_of(values.begin(), values.end(), [value](int item) { return item == value; }));
}

// Rows keep input/expected pairs together instead of forming a Cartesian product.
[[using gentest: test("clamp_rows"), parameters_pack((input, expected), (-1, 0), (0, 0), (5, 5), (10, 10), (11, 10))]]
inline void clampRows(int input, int expected) {
    gentest::expect_eq(std::clamp(input, 0, 10), expected);
}

// Two element types x two container templates produce four concrete cases.
template <typename T, template <class...> class Container>
[[using gentest: test("push_pop"), template(T, int, long), template(Container, std::vector, std::deque)]]
void pushPop() {
    Container<T> values;
    gentest::expect_true(values.empty());
    values.push_back(T{7});
    gentest::expect_eq(values.back(), T{7});
    values.pop_back();
    gentest::expect_true(values.empty());
}

} // namespace parameterized
