// Default main() for gentest test executables.
//
// Provides explicit registration via factory functions, then runs all tests.
// No static initializers — everything happens from main().
//
// Usage: link your test target against this file (or provide your own main).
//
//   add_executable(my_tests cases.cpp)
//   target_sources(my_tests PRIVATE gentest_main.cpp)
//   gentest_attach_codegen(my_tests)

#include "gentest/runner.h"

// Extern declaration filled in by generated registry.cpp.
extern void gentest__register_all();

int main(int argc, char **argv) {
    gentest__register_all();
    return gentest::run_all_tests(argc, argv);
}
