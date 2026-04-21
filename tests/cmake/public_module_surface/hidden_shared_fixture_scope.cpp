import gentest;

auto main() -> int {
    auto scope = gentest::detail::SharedFixtureScope::Suite;
    (void)scope;
    return 0;
}
