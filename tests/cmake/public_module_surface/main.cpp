#include <type_traits>

import gentest;
import public_module_surface.cases;

static_assert(sizeof(gentest::failure) > 0);
static_assert(sizeof(gentest::assertion) > 0);

struct example_exception {};

auto main(int argc, char **argv) -> int {
    constexpr auto combined_policy = gentest::LogPolicy::OnFailure | gentest::LogPolicy::Always;
    static_assert(combined_policy == gentest::LogPolicy::Always);
    static_assert(std::is_same_v<decltype(gentest::expect(true)), void>);
    static_assert(std::is_same_v<decltype(gentest::expect_true(true)), void>);
    static_assert(std::is_same_v<decltype(gentest::asserts::EXPECT_NO_THROW([] {})), void>);
    static_assert(std::is_same_v<decltype(gentest::asserts::EXPECT_THROW<example_exception>([] { throw example_exception{}; })), void>);
    static_assert(std::is_same_v<decltype(gentest::expect_eq(3.1415, gentest::approx::Approx(3.14).abs(0.01))), void>);

    auto policy = gentest::LogPolicy::Never;
    policy |= gentest::LogPolicy::OnFailure;
    policy |= gentest::LogPolicy::Always;
    if (gentest::to_underlying(policy) != gentest::to_underlying(gentest::LogPolicy::Always)) {
        return 1;
    }
    auto *fail_fn             = &gentest::fail;
    auto *registered_cases_fn = &gentest::registered_cases;
    (void)fail_fn;
    (void)registered_cases_fn;
    return gentest::run_all_tests(argc, argv);
}
