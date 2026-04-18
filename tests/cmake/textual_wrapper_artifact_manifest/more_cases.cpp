#include "gentest/attributes.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

namespace {

static int secondFileValue() { return 8; }

} // namespace

[[using gentest: test("textual_manifest/second_source")]]
static void secondSourceCase() {
    EXPECT_EQ(secondFileValue() * 5, 40);
}
