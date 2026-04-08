#include "gentest/attributes.h"

[[using gentest: test("smoke/axis_generators/range_single"), range(i, 5, 1, 5)]]
void range_single(int i) {
    (void)i;
}

[[using gentest: test("smoke/axis_generators/range_many"), range(i, 1, 2, 5)]]
void range_many(int i) {
    (void)i;
}

[[using gentest: test("smoke/axis_generators/range_descending_int"), range(i, 5, -2, 1)]]
void range_descending_int(int i) {
    (void)i;
}

[[using gentest: test("smoke/axis_generators/range_step_zero_int"), range(i, 1, 0, 3)]]
void range_step_zero_int(int i) {
    (void)i;
}

[[using gentest: test("smoke/axis_generators/range_float_many"), range(x, 0.0, 0.25, 0.5)]]
void range_float_many(double x) {
    (void)x;
}

[[using gentest: test("smoke/axis_generators/range_float_descending"), range(x, 1.0, -0.25, 0.5)]]
void range_float_descending(double x) {
    (void)x;
}

[[using gentest: test("smoke/axis_generators/linspace_single"), linspace(x, 0.5, 0.5, 1)]]
void linspace_single(double x) {
    (void)x;
}

[[using gentest: test("smoke/axis_generators/linspace_many"), linspace(x, 0.0, 1.0, 5)]]
void linspace_many(double x) {
    (void)x;
}

[[using gentest: test("smoke/axis_generators/linspace_many_int"), linspace(n, 1, 5, 3)]]
void linspace_many_int(int n) {
    (void)n;
}

[[using gentest: test("smoke/axis_generators/geom_single"), geom(n, 4, 3, 1)]]
void geom_single(int n) {
    (void)n;
}

[[using gentest: test("smoke/axis_generators/geom_many"), geom(n, 1, 2, 4)]]
void geom_many(int n) {
    (void)n;
}

[[using gentest: test("smoke/axis_generators/geom_many_float"), geom(g, 1.5, 2.0, 3)]]
void geom_many_float(double g) {
    (void)g;
}

[[using gentest: test("smoke/axis_generators/logspace_single"), logspace(v, -2, -2, 1)]]
void logspace_single(double v) {
    (void)v;
}

[[using gentest: test("smoke/axis_generators/logspace_many"), logspace(v, -3, 3, 7)]]
void logspace_many(double v) {
    (void)v;
}

[[using gentest: test("smoke/axis_generators/logspace_many_int"), logspace(v, 0, 3, 4, 2)]]
void logspace_many_int(int v) {
    (void)v;
}
