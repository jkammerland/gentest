// Negative scenario: type resides in an anonymous namespace.
// The generator should error with a message instructing to move it
// to a named namespace.

#include "gentest/mock.h"

namespace {
struct Hidden {
    virtual ~Hidden() = default;
    virtual void f() = 0;
};
} // anonymous namespace

static int _gentest_mock_anon_ns_odr() {
    gentest::mock<Hidden> m;
    (void)m;
    return 0;
}

