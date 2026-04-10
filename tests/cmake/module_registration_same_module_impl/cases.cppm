export module gentest.module_registration_same_module_impl;

import gentest;

export namespace module_registration_same_module_impl_ns {

[[using gentest: test("module/same_module_impl")]]
void same_module_case() {}

} // namespace module_registration_same_module_impl_ns
