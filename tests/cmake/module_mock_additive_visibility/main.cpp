#if defined(GENTEST_CODEGEN)
#include "gentest/runner.h"
#else
import gentest;
import gentest.additive_provider;
import gentest.additive_header_consumer;
#endif

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
