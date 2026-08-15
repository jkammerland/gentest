#include "async_local_fixture_teardown_noexceptions.hpp"

using namespace gentest::asserts;

namespace regressions::async_local_teardown_noexceptions {

gentest::async::manual_event first_waiting;
gentest::async::manual_event release_first;
gentest::async::manual_event keep_second_suspended;
gentest::async::manual_event plain_waiting;
gentest::async::manual_event release_plain;
gentest::async::manual_event keep_fixture_suspended;

gentest::async_test<void> fatal_assert(LocalFx &) {
    co_await gentest::async::yield();
    ASSERT_TRUE(false, "intentional async fatal assert in no-exception mode");
}

gentest::async_test<void> interleaved_first_fatal(FirstInterleavedFx &) {
    release_first.reset_all();
    keep_second_suspended.reset_all();
    first_waiting.set("first interleaved case suspended");
    co_await release_first.wait("second interleaved case released first");
    ASSERT_TRUE(false, "intentional interleaved async fatal assert in no-exception mode");
}

gentest::async_test<void> interleaved_second_holds_hook(SecondInterleavedFx &) {
    co_await first_waiting.wait("first interleaved case suspended");
    release_first.set("second interleaved case released first");
    co_await keep_second_suspended.wait("second case intentionally stays suspended");
}

gentest::async_test<void> plain_interleaved_fatal() {
    release_plain.reset_all();
    keep_fixture_suspended.reset_all();
    plain_waiting.set("plain interleaved case suspended");
    co_await release_plain.wait("fixture case released plain fatal case");
    ASSERT_TRUE(false, "intentional plain async fatal assert in no-exception mode");
}

gentest::async_test<void> plain_interleaved_fixture_holds_hook(SecondInterleavedFx &) {
    co_await plain_waiting.wait("plain interleaved case suspended");
    release_plain.set("fixture case released plain fatal case");
    co_await keep_fixture_suspended.wait("fixture case intentionally stays suspended");
}

} // namespace regressions::async_local_teardown_noexceptions
