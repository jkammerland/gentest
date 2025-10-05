#include "gentest/runner.h"

namespace [[using gentest: suite("skiponly")]] skiponly {

[[using gentest: test("alpha"), skip("not ready")]]
void alpha() {
    // skipped
}

[[using gentest: test("beta"), skip("flaky")]]
void beta() {
    // skipped
}

} // namespace skiponly
