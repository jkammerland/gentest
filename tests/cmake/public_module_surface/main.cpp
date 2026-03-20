import gentest;

static_assert(sizeof(gentest::failure) > 0);
static_assert(sizeof(gentest::assertion) > 0);

auto main() -> int {
    gentest::expect(true);
    auto* fail_fn             = &gentest::fail;
    auto* registered_cases_fn = &gentest::registered_cases;
    (void)fail_fn;
    (void)registered_cases_fn;
    return 0;
}
