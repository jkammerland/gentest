#if !defined(GENTEST_XREPO_USE_MODULES)
#include "gentest/runner.h"
#else
import gentest;
import downstream.xrepo.consumer_cases;
#endif

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
