export module public_module_surface.sync_cases;

import gentest;

export namespace public_module_surface::sync_cases {

[[using gentest: test("plain_sync")]]
void plain_sync_case() {
    auto context = gentest::diagnostic_context("module sync context");
    gentest::expect_true(true);
    gentest::expect_that(42, gentest::match::Eq(42));
    gentest::asserts::ASSERT_THAT(42, gentest::match::AllOf(gentest::match::Ge(40), gentest::match::Lt(43)));
}

} // namespace public_module_surface::sync_cases
