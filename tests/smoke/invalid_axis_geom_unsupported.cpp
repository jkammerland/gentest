#include "gentest/attributes.h"

#include <vector>

[[using gentest: test("smoke/invalid_axis/geom_unsupported"), geom(values, 1, 2, 3)]]
void geom_unsupported(std::vector<void *> values) {
    (void)values;
}
