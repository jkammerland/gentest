module;

export module gentest.story034.hidden_fixture;

import gentest;

namespace story034_hidden_fixture_detail {

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int  value = 0;
};

} // namespace story034_hidden_fixture_detail

export namespace story034_hidden_fixture {

[[using gentest: test("module_registration/hidden_fixture")]]
void hidden_fixture(story034_hidden_fixture_detail::Fixture &) {}

} // namespace story034_hidden_fixture
