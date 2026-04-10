module gentest.module_registration_rejects_non_interface;

import gentest;

namespace module_registration_rejects_non_interface_ns {

[[using gentest: test("module/rejects_non_interface")]]
void rejects_non_interface_case() {}

} // namespace module_registration_rejects_non_interface_ns
