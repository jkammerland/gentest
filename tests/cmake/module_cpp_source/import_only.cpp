import gentest;
import gentest.cpp_source_imported_cases;

using namespace gentest::asserts;

[[using gentest: test("cpp_source/import_only")]]
void importOnly() {
    const cpp_source::ImportedValue input{42};
    EXPECT_EQ(cpp_source::importedAnswer(input), 42);
}
