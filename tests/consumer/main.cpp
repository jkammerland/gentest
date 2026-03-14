#if defined(GENTEST_CODEGEN) || !defined(GENTEST_CONSUMER_USE_MODULES)
#include "gentest/runner.h"
#else
import gentest;
#endif

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
