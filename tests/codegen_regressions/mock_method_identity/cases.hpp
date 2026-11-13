#pragma once

#include "gentest/attributes.h"

namespace fixture {

[[using gentest: test("mock_method_identity/multiple_inheritance_signatures")]]
void multiple_inheritance_signatures();

[[using gentest: test("mock_method_identity/multiple_inheritance_same_signature")]]
void multiple_inheritance_same_signature();

[[using gentest: test("mock_method_identity/inherited_methods")]]
void inherited_methods();

[[using gentest: test("mock_method_identity/foreign_signature_death"), death]]
void foreign_signature_death();

} // namespace fixture
