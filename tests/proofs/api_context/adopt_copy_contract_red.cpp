#include "gentest/runner.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<gentest::CurrentContextLease>,
              "red-phase: gentest::CurrentContextLease must be non-copyable to keep context accounting balanced");
static_assert(!std::is_copy_assignable_v<gentest::CurrentContextLease>,
              "red-phase: gentest::CurrentContextLease must be non-copy-assignable to keep context accounting balanced");
static_assert(!std::is_move_constructible_v<gentest::CurrentContextLease>,
              "red-phase: gentest::CurrentContextLease must be non-movable to keep context accounting balanced");
static_assert(!std::is_move_assignable_v<gentest::CurrentContextLease>,
              "red-phase: gentest::CurrentContextLease must be non-move-assignable to keep context accounting balanced");

int main() { return 0; }
