import gentest;

using namespace gentest::asserts;

[[using gentest: test("cpp_source/import_only")]]
void importOnly() {
    EXPECT_TRUE(true);
}
