#include "gentest/runner.h"

namespace fixture_errors {

struct [[using gentest: fixture(suite)]] EnvSuite {
    // Illegal: suite fixtures should not declare member tests under strict mode
    [[using gentest: test("suite_member/illegal")]]
    void illegal() {
        gentest::expect(true, "should not be reached in strict mode");
    }
};

} // namespace fixture_errors
