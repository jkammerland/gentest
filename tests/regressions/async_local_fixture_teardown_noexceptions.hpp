#pragma once

#include "gentest/runner.h"

#include <cstdio>

namespace regressions::async_local_teardown_noexceptions {

extern gentest::async::manual_event first_waiting;
extern gentest::async::manual_event release_first;
extern gentest::async::manual_event keep_second_suspended;
extern gentest::async::manual_event plain_waiting;
extern gentest::async::manual_event release_plain;
extern gentest::async::manual_event keep_fixture_suspended;

struct LocalFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override { co_await gentest::async::yield(); }
    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};
struct FirstInterleavedFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override { co_await gentest::async::yield(); }
    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-first-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};
struct SecondInterleavedFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override { co_await gentest::async::yield(); }
    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-second-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/fatal_assert")]]
gentest::async_test<void> fatal_assert(LocalFx &);
[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/interleaved/00_first_fatal")]]
gentest::async_test<void> interleaved_first_fatal(FirstInterleavedFx &);
[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/interleaved/01_second_holds_hook")]]
gentest::async_test<void> interleaved_second_holds_hook(SecondInterleavedFx &);
[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/plain_interleaved/00_plain_fatal")]]
gentest::async_test<void> plain_interleaved_fatal();
[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/plain_interleaved/01_fixture_holds_hook")]]
gentest::async_test<void> plain_interleaved_fixture_holds_hook(SecondInterleavedFx &);

} // namespace regressions::async_local_teardown_noexceptions
