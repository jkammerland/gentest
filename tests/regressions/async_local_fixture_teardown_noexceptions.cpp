#include "gentest/runner.h"

#include <cstdio>

using namespace gentest::asserts;

namespace regressions::async_local_teardown_noexceptions {

struct LocalFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        co_return;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/fatal_assert")]]
gentest::async_test<void> fatal_assert(LocalFx &) {
    co_await gentest::async::yield();
    ASSERT_TRUE(false, "intentional async fatal assert in no-exception mode");
}

} // namespace regressions::async_local_teardown_noexceptions
