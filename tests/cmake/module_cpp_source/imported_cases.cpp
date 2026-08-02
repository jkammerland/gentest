module;

export module gentest.cpp_source_imported_cases;

import gentest;

using namespace gentest::asserts;

export namespace cpp_source {

[[using gentest: test("cpp_source/imported_case")]]
void importedCase() {
    EXPECT_TRUE(true);
}

} // namespace cpp_source
