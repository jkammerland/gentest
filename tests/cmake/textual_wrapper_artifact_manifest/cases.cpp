#include "gentest/attributes.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

namespace {

int anonymousValue() { return 17; }

static int staticValue() { return 25; }

} // namespace

[[using gentest: test("textual_manifest/anonymous_static")]]
static void anonymousStaticCase() {
    EXPECT_EQ(anonymousValue() + staticValue(), 42);
}

namespace textual_manifest {

struct LocalFixture : gentest::FixtureSetup {
    int value = 0;

    void setUp() override { value = 42; }
};

[[using gentest: test("textual_manifest/local_fixture")]]
void localFixtureCase(LocalFixture &fixture) {
    EXPECT_EQ(fixture.value, 42);
}

} // namespace textual_manifest
