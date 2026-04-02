import gentest;

static_assert(sizeof(gentest::failure) > 0);
static_assert(sizeof(gentest::assertion) > 0);

struct example_exception {};

auto main() -> int {
    using namespace gentest::asserts;

    gentest::expect(true);
    EXPECT_NO_THROW([] {});
    EXPECT_THROW<example_exception>([] { throw example_exception{}; });
    gentest::expect_eq(3.1415, gentest::approx::Approx(3.14).abs(0.01));
    auto *fail_fn             = &gentest::fail;
    auto *registered_cases_fn = &gentest::registered_cases;
    (void)fail_fn;
    (void)registered_cases_fn;
    return 0;
}
