module;
#include "gentest/attributes.h"

#define GENTEST_SCAN_SELECT(x) x

#if GENTEST_SCAN_SELECT(0)
import smoke.scan_wrong;
#endif

export module smoke.scan_unknown_condition_warn;

export namespace smoke::scan_unknown_condition_warn {

[[using gentest: test("smoke/scan_unknown_condition_warn")]]
void case_selected() {}

} // namespace smoke::scan_unknown_condition_warn
