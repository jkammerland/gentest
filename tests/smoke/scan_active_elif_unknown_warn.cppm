module;

#define GENTEST_SCAN_BRANCH() 0

#if 0
export module gentest.scan_active_elif_unknown_warn_dead;
#elif GENTEST_SCAN_BRANCH()
import gentest.scan_active_elif_unknown_warn_missing;
#endif

export module gentest.scan_active_elif_unknown_warn;
