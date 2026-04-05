module;

export /* primary */ module gentest.partition_import_cases;

export import /* partition */ :helpers;

import gentest;
using namespace gentest::asserts;

export namespace partition_import {

[[using gentest: test("partition/shorthand_import")]]
void shorthand_import() {
    EXPECT_EQ(partition_import::helper_value(), 17);
}

} // namespace partition_import
