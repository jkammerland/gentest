#include "gentest/attributes.h"

[[using gentest: test("smoke/invalid_axis/unknown_parameters_name"), parameters(missing, 1)]]
void unknown_parameters_name(int actual) {
    (void)actual;
}

[[using gentest: test("smoke/invalid_axis/unknown_pack_name"), parameters_pack((missing, rhs), (1, 2))]]
void unknown_pack_name(int lhs, int rhs) {
    (void)lhs;
    (void)rhs;
}

[[using gentest: test("smoke/invalid_axis/duplicate_axis"), parameters(i, 1), range(i, 1, 1, 3)]]
void duplicate_axis(int i) {
    (void)i;
}

[[using gentest: test("smoke/invalid_axis/overlap_pack"), parameters(i, 1), parameters_pack((i, j), (1, 2))]]
void overlap_pack(int i, int j) {
    (void)i;
    (void)j;
}

[[using gentest: test("smoke/invalid_axis/range_inconsistent"), range(i, 5, 1, 1)]]
void range_inconsistent(int i) {
    (void)i;
}
