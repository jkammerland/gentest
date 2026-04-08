#include "gentest/attributes.h"

[[using gentest: test("smoke/parameters_pack/rows"), parameters_pack((lhs, rhs), (1, 2), (3, 4))]]
void pack_rows(int lhs, int rhs) {
    (void)lhs;
    (void)rhs;
}

[[using gentest: test("smoke/parameters_pack/mixed"), parameters_pack((lhs, rhs), (10, 20), (30, 40)), parameters(scale, 7, 8)]]
void pack_mixed(int lhs, int rhs, int scale) {
    (void)lhs;
    (void)rhs;
    (void)scale;
}
