#include "gentest/attributes.h"

#include <vector>

[[using gentest: test("smoke/invalid_axis/linspace_unsupported"), linspace(values, 0, 1, 3)]]
void linspace_unsupported(std::vector<void *> values) {
    (void)values;
}
