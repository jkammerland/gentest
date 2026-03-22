module;

#define GENTEST_SCAN_BRANCH() 1

#if 1
export module gentest.scan_dead_elif_unknown_nowarn;
#elif GENTEST_SCAN_BRANCH()
import gentest.scan_dead_elif_unknown_nowarn_missing;
#endif
