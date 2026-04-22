import gentest;

struct HiddenFixture {};

auto main() -> int {
    gentest::detail::register_shared_fixture<HiddenFixture>(gentest::detail::SharedFixtureScope::Suite, "suite", "fixture");
    return 0;
}
