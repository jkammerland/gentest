#include "cases.hpp"

#include "gentest/runner.h"

namespace discovery_render_regressions {

using namespace gentest::asserts;

namespace {

struct Concrete final : Abstract {
    [[nodiscard]] int id() const override { return 7; }
};

} // namespace

NonCopyable &noncopyable_ref() {
    static NonCopyable instance;
    return instance;
}

const Abstract &abstract_ref() {
    static const Concrete instance;
    return instance;
}

void global_fixture_type(DrrGlobalFixture &fixture) { EXPECT_EQ(fixture.value, 11); }

void escapes(std::string_view text) {
    // "a\x41" is two characters; "nul\0x" is a C string that ends at the NUL.
    EXPECT_TRUE(text == "aA" || text == "nul");
}

void hex_range(int value) { EXPECT_TRUE(value >= 16 && value <= 18); }

void long_double_range(long double value) { EXPECT_TRUE(value == 0.0L || value == 0.25L || value == 0.5L); }

} // namespace discovery_render_regressions
