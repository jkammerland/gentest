#if !defined(GENTEST_DOWNSTREAM_USE_MODULES)
#include "gentest/runner.h"
#else
import gentest;
import downstream.bazel.consumer_cases;
#endif

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
