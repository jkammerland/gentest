#include "gentest/attributes.h"

namespace [[suite("smoke/namespace_diag/plain")]] plain_scope {

[[using gentest: test("smoke/namespace_diag/plain_one")]]
void plain_one() {}

[[using gentest: test("smoke/namespace_diag/plain_two")]]
void plain_two() {}

} // namespace plain_scope

namespace [[vendor::suite("smoke/namespace_diag/foreign")]] foreign_scope {

[[using gentest: test("smoke/namespace_diag/foreign_case")]]
void foreign_case() {}

} // namespace foreign_scope

namespace [[using gentest: suite("smoke/namespace_diag/dup_a"), suite("smoke/namespace_diag/dup_b")]] duplicate_scope {

[[using gentest: test("smoke/namespace_diag/duplicate_case")]]
void duplicate_case() {}

} // namespace duplicate_scope
