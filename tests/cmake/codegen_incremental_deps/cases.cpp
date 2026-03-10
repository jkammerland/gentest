#include "gentest/attributes.h"
#include "gentest/mock.h"
#include "gentest/runner.h"

#include "iface.hpp"

namespace depcase {

using namespace gentest::asserts;
using IfaceMock = gentest::mock<Iface>;

[[maybe_unused]] inline void instantiate_mock() {
    IfaceMock mock;
    (void)mock;
}

#if DEP_SWITCH
[[using gentest: test("incremental/compile/on")]]
void compile_variant_on() { EXPECT_TRUE(true); }
#else
[[using gentest: test("incremental/compile/off")]]
void compile_variant_off() { EXPECT_TRUE(true); }
#endif

} // namespace depcase
