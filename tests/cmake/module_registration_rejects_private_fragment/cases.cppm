export module gentest.module_registration_rejects_private_fragment;

import gentest;

export namespace module_registration_rejects_private_fragment_ns {

[[using gentest: test("module/rejects_private_fragment")]]
void rejects_private_fragment_case() {}

} // namespace module_registration_rejects_private_fragment_ns

module :private;

auto private_fragment_helper() -> int { return 0; }
