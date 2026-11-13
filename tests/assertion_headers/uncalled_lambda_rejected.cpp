// Compile-fail probe: a lambda-expression handed to a statement macro would be
// created but never called, so the macro must reject it at compile time.
// GENTEST_PROBE_VARIANT selects the macro under test.
#include "gentest/runner.h"

#include <stdexcept>

void probe() {
#if GENTEST_PROBE_VARIANT == 0
    EXPECT_NO_THROW([&] { throw std::runtime_error("never observed"); });
#elif GENTEST_PROBE_VARIANT == 1
    ASSERT_NO_THROW([&] { throw std::runtime_error("never observed"); });
#elif GENTEST_PROBE_VARIANT == 2
    EXPECT_THROW([&] { throw std::runtime_error("never observed"); }, std::runtime_error);
#elif GENTEST_PROBE_VARIANT == 3
    ASSERT_THROW([&] { throw std::runtime_error("never observed"); }, std::runtime_error);
#else
    // Control: the same TU compiles once the lambda is called.
    EXPECT_NO_THROW([&] { throw std::runtime_error("observed"); }());
#endif
}
