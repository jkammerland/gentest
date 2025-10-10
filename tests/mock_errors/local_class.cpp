// Negative scenario: local class inside a function.
// The generator should error with a message instructing to move it
// to namespace scope.

#include "gentest/mock.h"

static int _gentest_mock_local_class_odr() {
    struct Local { void g(){} };
    gentest::mock<Local> m;
    (void)m;
    return 0;
}

