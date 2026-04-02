#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/dep_mocks.hpp"

namespace depcase {

using namespace gentest::asserts;

[[maybe_unused]] inline void instantiate_mock() {
    mocks::IfaceMock mock;
    (void)mock;
}

#if DEP_SWITCH
[[using gentest: test("incremental/compile/on")]]
void compile_variant_on() {
    EXPECT_TRUE(true);
}
#else
[[using gentest: test("incremental/compile/off")]]
void compile_variant_off() {
    EXPECT_TRUE(true);
}
#endif

} // namespace depcase
