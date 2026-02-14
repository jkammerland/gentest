#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

#include <stdexcept>

namespace ctor {

// Ephemeral (member) fixture with throwing constructor
struct Thrower {
    Thrower() { throw std::runtime_error("ephemeral-ctor"); }
    [[using gentest: test("ephemeral_member")]] void t() {}
};

// Free-function fixtures: constructor throws
struct FreeFx { FreeFx() { throw std::runtime_error("free-fx-ctor"); } };
[[using gentest: test("free_fixtures")]]
void free_uses_throwing_fixture(FreeFx&) {}

// Suite-level fixture with throwing constructor
struct [[using gentest: fixture(suite)]] BadSuite {
    BadSuite() { throw std::runtime_error("suite-ctor"); }
    [[using gentest: test("suite")]] void a() {}
};

// Global fixture with throwing constructor
struct [[using gentest: fixture(global)]] BadGlobal {
    BadGlobal() { throw std::runtime_error("global-ctor"); }
    [[using gentest: test("global")]] void a() {}
};

} // namespace ctor
