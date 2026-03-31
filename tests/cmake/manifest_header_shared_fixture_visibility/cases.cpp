#include "shared_fixture.hpp"

#include <gentest/runner.h>

using namespace gentest::asserts;

namespace manifest_fixture {

[[using gentest: test("manifest/header_shared_fixture")]]
inline void header_shared_fixture(SharedFixture &fixture) {
    EXPECT_EQ(fixture.value, 7);
}

} // namespace manifest_fixture
