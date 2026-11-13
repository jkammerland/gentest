#pragma once

#include "gentest/attributes.h"

namespace fixture {

[[using gentest: test("mock_argument_shapes/incomparable_parameters")]]
void incomparable_parameters();

[[using gentest: test("mock_argument_shapes/with_owns_text")]]
void with_owns_text();

[[using gentest: test("mock_argument_shapes/with_converts_values")]]
void with_converts_values();

[[using gentest: test("mock_argument_shapes/text_mismatch_death"), death]]
void text_mismatch_death();

} // namespace fixture
