module;

export module gentest.cpp_source_imported_cases;

import gentest;

using namespace gentest::asserts;

export namespace cpp_source {

struct ImportedValue {
    int value = 0;
};

int importedAnswer(const ImportedValue &input) { return input.value; }

[[using gentest: test("cpp_source/imported_case")]]
void importedCase() {
    EXPECT_EQ(importedAnswer(ImportedValue{42}), 42);
}

} // namespace cpp_source
