#include "local_fixture_teardown_noexceptions.hpp"

using namespace gentest::asserts;

namespace regressions::local_teardown_noexceptions {

void fatal_assert(LocalFx &) { ASSERT_TRUE(false, "intentional fatal assert in no-exception mode"); }

} // namespace regressions::local_teardown_noexceptions
