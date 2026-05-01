#include "gentest/runner.h"

#include <cstdio>

using namespace gentest::asserts;

namespace regressions::async_local_teardown_noexceptions {

gentest::async::manual_event first_waiting;
gentest::async::manual_event release_first;
gentest::async::manual_event keep_second_suspended;
gentest::async::manual_event plain_waiting;
gentest::async::manual_event release_plain;
gentest::async::manual_event keep_fixture_suspended;

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

struct FirstInterleavedFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        co_return;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-first-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};

struct SecondInterleavedFx : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        co_return;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        (void)std::fputs("async-local-fixture-teardown-noexc-second-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/fatal_assert")]]
gentest::async_test<void> fatal_assert(LocalFx &) {
    co_await gentest::async::yield();
    ASSERT_TRUE(false, "intentional async fatal assert in no-exception mode");
}

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/interleaved/00_first_fatal")]]
gentest::async_test<void> interleaved_first_fatal(FirstInterleavedFx &) {
    release_first.reset_all();
    keep_second_suspended.reset_all();
    first_waiting.set("ready");
    co_await release_first.wait("ready");
    ASSERT_TRUE(false, "intentional interleaved async fatal assert in no-exception mode");
}

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/interleaved/01_second_holds_hook")]]
gentest::async_test<void> interleaved_second_holds_hook(SecondInterleavedFx &) {
    co_await first_waiting.wait("ready");
    release_first.set("ready");
    co_await keep_second_suspended.wait("second case intentionally stays suspended");
}

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/plain_interleaved/00_plain_fatal")]]
gentest::async_test<void> plain_interleaved_fatal() {
    release_plain.reset_all();
    keep_fixture_suspended.reset_all();
    plain_waiting.set("ready");
    co_await release_plain.wait("ready");
    ASSERT_TRUE(false, "intentional plain async fatal assert in no-exception mode");
}

[[using gentest: test("regressions/async_local_fixture_teardown_noexceptions/plain_interleaved/01_fixture_holds_hook")]]
gentest::async_test<void> plain_interleaved_fixture_holds_hook(SecondInterleavedFx &) {
    co_await plain_waiting.wait("ready");
    release_plain.set("ready");
    co_await keep_fixture_suspended.wait("fixture case intentionally stays suspended");
}

} // namespace regressions::async_local_teardown_noexceptions
