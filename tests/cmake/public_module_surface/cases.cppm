export module public_module_surface.cases;

import gentest;

export namespace public_module_surface {

[[using gentest: test("expect_true")]]
void expect_true_case() {
    gentest::expect_true(true);
}

} // namespace public_module_surface
