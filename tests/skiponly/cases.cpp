#include "gentest/runner.h"

namespace skiponly {

[[using gentest: test("skiponly/alpha"), skip("not ready")]]
void alpha() {
    // skipped
}

[[using gentest: test("skiponly/beta"), skip("flaky")]]
void beta() {
    // skipped
}

} // namespace skiponly
