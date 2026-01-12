#include "fixtures/fixtures.hpp"

namespace fixtures {

[[using gentest: test("free/basic"), fixtures(A, B<int>, C)]]
void free_basic(A &a, B<int> &b, C &c) {
    // setUp must have run for A
    gentest::expect_eq(a.phase, 1, "A setUp ran");
    a.phase = 2; // allow tearDown to validate
    gentest::expect(b.x == 0, "B default value");
    gentest::expect(std::string(b.msg) == "ok", "B default value");
    gentest::expect_eq(c.v, 7, "C default value");
}

} // namespace fixtures
