#include "cases.hpp"

#include "gentest/detail/runtime_context.h"

#include <array>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace diagnostics_tests {

void container_failure() { gentest::expect_eq(std::vector{1, 2, 3}, std::vector{1, 9, 3}); }

void string_failure() { gentest::expect_eq(std::string("a\nb\0", 4), std::string("a\nc\0", 4)); }

void check_packet() { gentest::expect_that(std::vector{1, 9}, gentest::match::ElementsAre(1, 2)); }

void scoped_failure() {
    auto packet = gentest::diagnostic_context("packet", 41);
    {
        auto attempt = gentest::diagnostic_context("attempt", 3);
        check_packet();
    }
    gentest::expect(false, "outer scope only");
}

void scope_unwinding() {
    try {
        auto retired = gentest::diagnostic_context("retired context");
        throw std::runtime_error("unwind");
    } catch (const std::runtime_error &) {}
    gentest::expect(false, "after unwinding");
}

void fatal_matcher() {
    auto context = gentest::diagnostic_context("fatal context");
    gentest::require_that("bad", gentest::match::StartsWith("ok"));
    gentest::fail("fatal matcher continued");
}

void matcher_success() {
    using namespace gentest::match;
    gentest::expect_that("error: details", AllOf(StartsWith("error:"), StrContains("details"), Not(EndsWith("!"))));
    gentest::expect_that(4, AnyOf(Eq(3), InRange(4, 6)));
    gentest::expect_that(std::array{1, 2, 3}, ElementsAre(1, Ge(2), 3));
    gentest::require_that(std::vector<int>{}, ElementsAre());
    gentest::expect_that(nullptr, Not(StartsWith("prefix")));

    int calls = 0;
    gentest::expect_that(std::array{2}, ElementsAre([&](const int &value) {
                             ++calls;
                             return value == 2;
                         }));
    gentest::expect_eq(calls, 1);
    gentest::expect_that(9, [&](const int &value) {
        ++calls;
        return value == 9;
    });
    gentest::expect_eq(calls, 2);
}

void native_elements() {
    gentest::mock<mocking::TemplateTemplateFixed> mock;
    gentest::expect(mock, &mocking::TemplateTemplateFixed::template take<std::array>).where_args(gentest::match::ElementsAre(1, 2));
    mock.template take<std::array>(std::array{1, 9});
}

void matcher_single_evaluation() {
    int                               calls = 0;
    gentest::match::ArgPredicate<int> predicate;
    predicate.evaluate = [&](const int &) -> gentest::match::MatchResult {
        ++calls;
        return {false, "single evaluation mismatch"};
    };
    gentest::expect_that(9, predicate);
    gentest::expect_that(9, gentest::match::AllOf(gentest::match::Ge(0), predicate));
    gentest::expect_that(std::array{9}, gentest::match::ElementsAre(predicate));
    gentest::expect_eq(calls, 3);
}

void matcher_failures(int mode) {
    using gentest::match::ElementsAre;
    switch (mode) {
    case 0: gentest::expect_that(std::array{1}, ElementsAre()); break;
    case 1: gentest::expect_that(std::vector<int>{}, ElementsAre(1)); break;
    case 2: gentest::expect_that(std::vector<std::vector<int>>{{1, 2}}, ElementsAre(ElementsAre(1, 3))); break;
    }
}

void context_success() {
    auto        context = gentest::diagnostic_context("success-only secret");
    const auto  owner   = gentest::get_current_context();
    std::thread worker([owner] {
        auto lease = gentest::set_current_context(owner);
        gentest::log("adopted worker log");
    });
    worker.join();
    gentest::expect(true);
}

gentest::awaitable<void> child(int id, bool fail) {
    auto context = gentest::diagnostic_context("child", id);
    co_await gentest::async::yield();
    gentest::expect(!fail, fmt::format("child {} assertion", id));
}

gentest::awaitable<void> async_contexts() {
    auto parent = gentest::diagnostic_context("request", 7);
    co_await gentest::async::when_all(child(1, true), child(2, true));
    gentest::expect(false, "parent assertion");
}

gentest::awaitable<void> async_context_success() {
    auto parent = gentest::diagnostic_context("success-only async secret");
    co_await gentest::async::when_all(child(1, false), child(2, false));
}

struct DestructorAssertion {
    ~DestructorAssertion() { gentest::expect(false, "canceled child destructor"); }
};

gentest::awaitable<void> suspended_child() {
    auto                context = gentest::diagnostic_context("loser", 1);
    DestructorAssertion assertion;
    co_await std::suspend_always{};
}

void cancellation_contexts() {
    auto parent              = gentest::diagnostic_context("request", 7);
    auto task                = suspended_child();
    auto frame               = task.frame();
    frame->framework_context = gentest::detail::current_test();
    {
        gentest::detail::AsyncFrameScope scope(frame);
        frame->handle.resume();
    }
    gentest::require(!task.exception() && !frame->handle.done(), "child must suspend with its lexical context alive");
    task = {};
    frame.reset();
    gentest::expect(false, "parent after cancellation");
}

void mock_source(int mode) {
    gentest::mock<mocking::Calculator> mock;
    if (mode == 4) {
        const auto location = std::source_location::current();
        EXPECT_CALL(mock, compute).times(1).with(1, 1).returns(7);
        gentest::log(fmt::format("declared line {}", location.line() + 1));
        (void)mock.compute(2, 1);
        return;
    }

    const auto location    = std::source_location::current();
    auto       expectation = gentest::expect(mock, &mocking::Calculator::compute);
    expectation.times(1).with(1, 1).returns(7);
    gentest::log(fmt::format("declared line {}", location.line() + 1));
    if (mode == 0)
        return;
    (void)mock.compute(mode == 1 ? 2 : 1, 1);
    if (mode == 2)
        (void)mock.compute(1, 1);
    if (mode == 3)
        gentest::expect(mock, &mocking::Calculator::compute).times(1).returns(8);
    if (mode == 5)
        expectation.times(2);
}

} // namespace diagnostics_tests
