module;

export module gentest.partition_registration_visibility:tests;

#if !defined(GENTEST_CODEGEN)
import gentest;
using namespace gentest::asserts;
#endif

export namespace partition_visibility {

inline int partition_local_value() { return 41; }

[[using gentest: test("partition/registration_visibility")]]
void partition_registration_visibility() {
#if !defined(GENTEST_CODEGEN)
    EXPECT_EQ(partition_local_value(), 41);
#endif
}

} // namespace partition_visibility
