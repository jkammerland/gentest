module;

export module gentest.partition_registration_visibility:tests;

import gentest;
using namespace gentest::asserts;

export namespace partition_visibility {

inline int partition_local_value() { return 41; }

[[using gentest: test("partition/registration_visibility")]]
void partition_registration_visibility() {
    EXPECT_EQ(partition_local_value(), 41);
}

} // namespace partition_visibility
