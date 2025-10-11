#include "gentest/runner.h"

namespace [[using gentest: suite("fixture_errors")]] fx_suite {

struct [[using gentest: fixture(suite)]] EnvSuite {
    // Illegal: suite fixtures should not declare member tests under strict mode
    [[using gentest: test("fixture_errors/suite_member/illegal")]]
    void illegal() {
        gentest::expect(true, "should not be reached in strict mode");
    }
};

} // namespace fx_suite

