module;

struct GlobalFragmentFixture {};

export module gentest.story034.global_fragment_fixture;

import gentest;

export namespace story034_global_fragment_fixture {

[[using gentest: test("module_registration/global_fragment_fixture")]]
void global_fragment_fixture(GlobalFragmentFixture &) {}

} // namespace story034_global_fragment_fixture
