module;

export module gentest.module_registration_rejects_global_fragment;

import gentest;

export namespace module_registration_rejects_global_fragment_ns {

[[using gentest: test("module/rejects_global_fragment")]]
void rejects_global_fragment_case() {}

} // namespace module_registration_rejects_global_fragment_ns
