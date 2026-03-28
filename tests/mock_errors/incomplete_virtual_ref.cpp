// Negative scenario: refer to a mock for a type that is only forward-declared
// (definition is not visible in this TU). The generator should fail with an
// informative message about incomplete target types.

#include "gentest/mock.h"

struct ForwardOnly; // intentionally incomplete here

using ForwardOnlyMock = gentest::mock<ForwardOnly>;
