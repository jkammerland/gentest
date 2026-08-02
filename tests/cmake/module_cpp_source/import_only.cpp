import gentest;
import gentest.cpp_source_imported_cases;

using namespace gentest::asserts;

[[using gentest: test("cpp_source/import_only")]]
void importOnly() {
    EXPECT_TRUE(true);
}
