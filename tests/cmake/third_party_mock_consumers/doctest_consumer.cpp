#define DOCTEST_CONFIG_IMPLEMENT

#include "public/real_gmock_mocks.hpp"

#include <doctest/doctest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool has_arg(int argc, char **argv, std::string_view expected) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string_view{argv[i]} == expected) {
            return true;
        }
    }
    return false;
}

int run_gmock_failure_probe() {
    {
        ::testing::MockFunction<void()> callback;
        EXPECT_CALL(callback, Call()).Times(1);
    }
    return ::testing::Test::HasFailure() ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    if (has_arg(argc, argv, "--gentest-probe-gmock-failure")) {
        return run_gmock_failure_probe();
    }
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int doctest_rc = context.run();
    if (::testing::Test::HasFailure()) {
        return doctest_rc == 0 ? 1 : doctest_rc;
    }
    return doctest_rc;
}

TEST_CASE("generated gmock mock is usable from doctest") {
    using fixture::third_party::Calculator;
    using fixture::third_party::mocks::CalculatorMock;

    CalculatorMock mock;
    Calculator    *calculator = &mock;

    EXPECT_CALL(mock, add(4, 6)).WillOnce(::testing::Return(10));
    CHECK(calculator->add(4, 6) == 10);

    EXPECT_CALL(mock, increment(41)).WillOnce(::testing::Return(42));
    CHECK(mock.increment() == 42);

    EXPECT_CALL(mock, name()).WillOnce(::testing::Return(std::string{"doctest"}));
    CHECK(calculator->name() == "doctest");
}

TEST_CASE("generated gmock mock keeps indirect template method types") {
    using fixture::third_party::ResourceFactory;
    using fixture::third_party::aliases::IntPair;
    using fixture::third_party::aliases::PairBatch;
    using fixture::third_party::mocks::ResourceFactoryMock;
    using ::testing::ByMove;
    using ::testing::ElementsAre;
    using ::testing::Pointee;
    using ::testing::Return;
    using ::testing::ReturnRef;

    ResourceFactoryMock mock;
    ResourceFactory    *factory = &mock;

    EXPECT_CALL(mock, make("doctest")).WillOnce(Return(ByMove(std::make_unique<int>(11))));
    auto owned = factory->make("doctest");
    REQUIRE(owned != nullptr);
    CHECK(*owned == 11);

    int stored = 29;
    EXPECT_CALL(mock, value()).WillOnce(ReturnRef(stored));
    CHECK(&factory->value() == &stored);

    EXPECT_CALL(mock, combine(ElementsAre(IntPair{5, 8}, IntPair{13, 21}))).WillOnce(Return(IntPair{18, 29}));
    CHECK(factory->combine(PairBatch{IntPair{5, 8}, IntPair{13, 21}}) == IntPair{18, 29});

    EXPECT_CALL(mock, consume(Pointee(37))).WillOnce(Return(true));
    CHECK(factory->consume(std::make_unique<int>(37)));
}

TEST_CASE("generated gmock mock keeps inherited virtual aliases") {
    using fixture::third_party::InheritedWorkflow;
    using fixture::third_party::aliases::IntPair;
    using fixture::third_party::mocks::InheritedWorkflowMock;
    using ::testing::Return;

    InheritedWorkflowMock mock;
    InheritedWorkflow    *workflow = &mock;

    EXPECT_CALL(mock, inherited(3)).WillOnce(Return(5));
    CHECK(workflow->inherited(3) == 5);

    EXPECT_CALL(mock, label()).WillOnce(Return(std::string{"doctest"}));
    CHECK(workflow->label() == "doctest");

    EXPECT_CALL(mock, finish(IntPair{8, 13})).WillOnce(Return(21));
    CHECK(workflow->finish(IntPair{8, 13}) == 21);
}
