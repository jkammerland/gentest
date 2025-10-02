#include "gentest/runner.h"

namespace failing {

[[using gentest: test("failing/single")]]
void will_fail() {
    throw std::runtime_error("boom");
}

} // namespace failing