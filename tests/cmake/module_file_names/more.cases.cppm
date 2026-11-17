// The extra dot in this file name is the point of the fixture: codegen must
// still register the cases of this module unit.
export module module_file_names.more_cases;

import gentest;

export namespace module_file_names::more_cases {

[[using gentest: test("module_file_names/dotted")]]
void dotted() {
    gentest::expect_true(1 + 1 == 2);
}

} // namespace module_file_names::more_cases
