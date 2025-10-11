#include "gentest/runner.h"

namespace [[using gentest: suite("fixture_errors")]] fx_global {

struct [[using gentest: fixture(global)]] EnvGlobal {
    // Illegal: global fixtures should not declare member tests under strict mode
    [[using gentest: test("fixture_errors/global_member/illegal")]]
    void illegal() {
        gentest::expect(true, "should not be reached in strict mode");
    }
};

} // namespace fx_global

