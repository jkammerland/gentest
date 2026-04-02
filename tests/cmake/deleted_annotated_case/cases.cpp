#include "gentest/runner.h"

namespace deleted_annotated_case {

[[using gentest: test("live")]]
void live_case() {}

[[using gentest: test("deleted")]]
void deleted_case() = delete;

} // namespace deleted_annotated_case
