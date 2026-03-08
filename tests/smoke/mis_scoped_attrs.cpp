#include "gentest/attributes.h"

[[gentest::test("smoke/mis_scoped/death"), death]]
void mixed_scoped_and_unscoped_death() {}

[[gentest::test("smoke/mis_scoped/req"), req("REQ-MIS-SCOPED")]]
void mixed_scoped_and_unscoped_req() {}

[[test("smoke/mis_scoped/plain"), death]]
void fully_unscoped_gentest_attrs() {}
