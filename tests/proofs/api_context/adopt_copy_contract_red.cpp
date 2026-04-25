#include "gentest/runner.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<gentest::Adoption>,
              "red-phase: gentest::Adoption must be non-copyable to keep adopted context accounting balanced");
static_assert(!std::is_copy_assignable_v<gentest::Adoption>,
              "red-phase: gentest::Adoption must be non-copy-assignable to keep adopted context accounting balanced");

int main() { return 0; }
