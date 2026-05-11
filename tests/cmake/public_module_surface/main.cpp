#include <type_traits>

import gentest;
import public_module_surface.cases;

static_assert(sizeof(gentest::failure) > 0);
static_assert(sizeof(gentest::assertion) > 0);

struct example_exception {};

auto main(int argc, char **argv) -> int {
    static_assert(std::is_same_v<decltype(gentest::expect(true)), void>);
    static_assert(std::is_same_v<decltype(gentest::expect_true(true)), void>);
    static_assert(std::is_same_v<decltype(gentest::asserts::EXPECT_NO_THROW([] {})), void>);
    static_assert(std::is_same_v<decltype(gentest::asserts::EXPECT_THROW<example_exception>([] { throw example_exception{}; })), void>);
    static_assert(std::is_same_v<decltype(gentest::expect_eq(3.1415, gentest::approx::Approx(3.14).abs(0.01))), void>);
    static_assert(!std::is_copy_constructible_v<gentest::LogSinkHandle>);
    static_assert(std::is_move_constructible_v<gentest::LogSinkHandle>);

    auto *fail_fn             = &gentest::fail;
    auto *add_log_sink_fn     = &gentest::add_log_sink;
    auto *remove_log_sink_fn  = &gentest::remove_log_sink;
    auto *registered_cases_fn = &gentest::registered_cases;
    (void)fail_fn;
    (void)add_log_sink_fn;
    (void)remove_log_sink_fn;
    (void)registered_cases_fn;
    return gentest::run_all_tests(argc, argv);
}
