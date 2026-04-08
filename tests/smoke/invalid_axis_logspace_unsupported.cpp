#include "gentest/attributes.h"

#include <vector>

[[using gentest: test("smoke/invalid_axis/logspace_unsupported"), logspace(values, 0, 2, 3)]]
void logspace_unsupported(std::vector<void *> values) {
    (void)values;
}
