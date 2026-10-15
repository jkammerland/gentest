#pragma once

#include "gentest/attributes.h"

namespace bench_util_tests {
template <class T>
[[using gentest: test("floating_preservation"), template(T, float, double, long double)]]
void floating_preservation();
} // namespace bench_util_tests
