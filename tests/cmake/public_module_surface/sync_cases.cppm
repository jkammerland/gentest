export module public_module_surface.sync_cases;

import gentest;

export namespace public_module_surface::sync_cases {

[[using gentest: test("plain_sync")]]
void plain_sync_case() {
    gentest::expect_true(true);
}

} // namespace public_module_surface::sync_cases
