module;
#include "gentest/attributes.h"

#define GENTEST_SCAN_ENABLED 1

#if GENTEST_SCAN_ENABLED == 0
import smoke.scan_supported_wrong;
#endif

export module smoke.scan_supported_condition_nowarn;

export namespace smoke::scan_supported_condition_nowarn {

[[using gentest: test("smoke/scan_supported_condition_nowarn")]]
void case_selected() {}

} // namespace smoke::scan_supported_condition_nowarn
