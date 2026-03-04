#include "gentest/runner.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<gentest::ctx::Adopt>,
              "red-phase: gentest::ctx::Adopt must be non-copyable to keep adopted_tokens accounting balanced");
static_assert(!std::is_copy_assignable_v<gentest::ctx::Adopt>,
              "red-phase: gentest::ctx::Adopt must be non-copy-assignable to keep adopted_tokens accounting balanced");

int main() { return 0; }
