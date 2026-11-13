#include "cases.hpp"

#include "gentest/assertions.h"
#include "gentest/runner.h"
#include "public/mock_actions_results_mocks.hpp"

#include <memory>
#include <string>
#include <utility>

namespace fixture {

void non_default_constructible_result() {
    gentest::mock<Factory> mock_factory;
    EXPECT_CALL(mock_factory, make_token).times(2).returns(Token{5});

    Factory &factory = mock_factory;
    gentest::expect_eq(factory.make_token().value, 5, "first result");
    gentest::expect_eq(factory.make_token().value, 5, "a copyable result is returned on every call");
}

void move_only_result() {
    gentest::mock<Factory> mock_factory;
    EXPECT_CALL(mock_factory, make_job).times(1).returns(std::make_unique<Job>(Job{3}));

    Factory                   &factory = mock_factory;
    const std::unique_ptr<Job> job     = factory.make_job();
    gentest::expect(job != nullptr, "move-only result is handed over");
    gentest::expect_eq(job ? job->id : 0, 3, "move-only result value");

    gentest::mock<Factory> mock_other;
    EXPECT_CALL(mock_other, make_job).times(2).invokes([owned = std::make_unique<Job>(Job{8})]() { return std::make_unique<Job>(*owned); });
    Factory &other = mock_other;
    gentest::expect_eq(other.make_job()->id, 8, "move-only action, first call");
    gentest::expect_eq(other.make_job()->id, 8, "move-only action, second call");
}

void stateful_action() {
    gentest::mock<Factory> mock_factory;
    EXPECT_CALL(mock_factory, next).times(3).invokes([n = 0]() mutable { return n++; });

    Factory &factory = mock_factory;
    gentest::expect_eq(factory.next(), 0, "first call");
    gentest::expect_eq(factory.next(), 1, "state survives the first call");
    gentest::expect_eq(factory.next(), 2, "state survives the second call");
}

void out_parameters() {
    gentest::mock<Factory> mock_factory;
    EXPECT_CALL(mock_factory, parse).times(1).invokes([](const std::string &text, int &out) {
        out = std::stoi(text) + 1;
        return true;
    });

    Factory &factory = mock_factory;
    int      parsed  = 0;
    gentest::expect(factory.parse("41", parsed), "action result");
    gentest::expect_eq(parsed, 42, "the action writes through the reference parameter");
}

void owning_arguments() {
    gentest::mock<Factory> mock_factory;
    int                    consumed = 0;
    std::unique_ptr<Job>   kept;
    EXPECT_CALL(mock_factory, consume).times(1).invokes([&](Buf &&buffer) {
        const Buf taken(std::move(buffer));
        consumed = taken.size;
    });
    EXPECT_CALL(mock_factory, submit).times(1).invokes([&](std::unique_ptr<Job> job) { kept = std::move(job); });

    Factory &factory = mock_factory;
    Buf      buffer(9);
    factory.consume(std::move(buffer));
    factory.submit(std::make_unique<Job>(Job{6}));
    gentest::expect_eq(consumed, 9, "rvalue argument is moved into the action");
    gentest::expect_eq(buffer.size, -1, "rvalue argument was consumed"); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    gentest::expect_eq(kept ? kept->id : 0, 6, "by-value move-only argument is moved into the action");
}

void reference_without_action_death() {
    gentest::mock<Factory> mock_factory;
    EXPECT_CALL(mock_factory, slot).times(1);
    (void)mock_factory.slot();
}

void unexpected_reference_call_death() {
    gentest::mock<Factory> mock_factory;
    (void)mock_factory.slot();
}

void unexpected_value_call_death() {
    gentest::mock<Factory> mock_factory;
    (void)mock_factory.make_token();
}

} // namespace fixture
