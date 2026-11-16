#include "gentest/attributes.h"

// Namespace attribute diagnostics are reported once per namespace, however
// many cases and fixtures the namespace contains.

// A suite(...) without the gentest namespace is reported as mis-scoped.
namespace [[suite("smoke/namespace_diag/plain")]] plain_scope {

[[using gentest: test("smoke/namespace_diag/plain_one")]]
void plain_one() {}

[[using gentest: test("smoke/namespace_diag/plain_two")]]
void plain_two() {}

} // namespace plain_scope

// Another vendor's attribute is left to the compiler.
namespace [[vendor::suite("smoke/namespace_diag/foreign")]] foreign_scope {

[[using gentest: test("smoke/namespace_diag/foreign_case")]]
void foreign_case() {}

} // namespace foreign_scope

// gentest's suite(...) was removed in 2.0.0. A repeated attribute is reported once.
namespace [[using gentest: suite("smoke/namespace_diag/dup_a"), suite("smoke/namespace_diag/dup_b")]] duplicate_scope {

[[using gentest: test("smoke/namespace_diag/duplicate_case")]]
void duplicate_case() {}

} // namespace duplicate_scope

// Cases and a suite fixture all check the namespace; it is still reported once.
namespace [[gentest::suite("smoke/namespace_diag/removed")]] removed_scope {

struct [[gentest::fixture(suite)]] SharedState {
    int uses = 0;
};

[[gentest::test("smoke/namespace_diag/removed_one")]]
void removed_one(SharedState &state) {
    ++state.uses;
}

[[gentest::test("smoke/namespace_diag/removed_two")]]
void removed_two(SharedState &state) {
    ++state.uses;
}

} // namespace removed_scope
