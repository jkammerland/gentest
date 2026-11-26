#if defined(_WIN32)
#include "runner_entry.h"

auto main() -> int { return gentest::detail::runWindowsMain(); }
#else
#include "gentest/runner.h"

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
#endif
