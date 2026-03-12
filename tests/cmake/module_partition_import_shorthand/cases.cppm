module;

export module gentest.partition_import_cases;

export import :helpers;

#if !defined(GENTEST_CODEGEN)
import gentest;
using namespace gentest::asserts;
#endif

export namespace partition_import {

[[using gentest: test("partition/shorthand_import")]]
void shorthand_import() {
#if !defined(GENTEST_CODEGEN)
    EXPECT_EQ(partition_import::helper_value(), 17);
#endif
}

} // namespace partition_import
