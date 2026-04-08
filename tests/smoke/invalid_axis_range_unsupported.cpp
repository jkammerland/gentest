#include "gentest/attributes.h"

#include <vector>

[[using gentest: test("smoke/invalid_axis/range_unsupported"), range(values, 1, 1, 3)]]
void range_unsupported(std::vector<void *> values) {
    (void)values;
}
