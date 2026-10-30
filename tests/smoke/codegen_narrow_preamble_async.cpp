#include "gentest/runner.h"

namespace smoke::narrow_preamble {

[[using gentest: test("narrow/full/async")]]
gentest::awaitable<void> asynchronous_case() {
    co_return;
}

} // namespace smoke::narrow_preamble
