#pragma once

#include "gentest/runner.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace integration {

namespace math {

int fibonacci(int n);

[[using gentest: test("math/fibonacci"), slow, linux]]
void fibonacci_sequence();

} // namespace math

namespace registry {

[[using gentest: test("registry/map")]]
void map_behaviour();

} // namespace registry

namespace errors {

[[using gentest: test("errors/recover"), req("BUG-123"), owner("team-runtime")]]
void detect_and_recover_error();

[[using gentest: test("errors/throw"), skip("unstable"), windows]]
void throw_error();

} // namespace errors

} // namespace integration
