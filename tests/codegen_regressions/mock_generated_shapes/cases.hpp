#pragma once

#include "gentest/attributes.h"

namespace fixture {

[[using gentest: test("mock_generated_shapes/parameter_names")]]
void parameter_names();

[[using gentest: test("mock_generated_shapes/parameter_packs")]]
void parameter_packs();

[[using gentest: test("mock_generated_shapes/operator_templates")]]
void operator_templates();

[[using gentest: test("mock_generated_shapes/qualified_expressions")]]
void qualified_expressions();

} // namespace fixture
