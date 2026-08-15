#include "cases.hpp"

#include "gentest/runner.h"
#include "public/dep_mocks.hpp"

namespace depcase {

using namespace gentest::asserts;

[[maybe_unused]] inline void instantiate_mock() {
    mocks::IfaceMock mock;
    (void)mock;
}

#if DEP_SWITCH
void compile_variant_on() { EXPECT_TRUE(true); }
#else
void compile_variant_off() { EXPECT_TRUE(true); }
#endif

} // namespace depcase
