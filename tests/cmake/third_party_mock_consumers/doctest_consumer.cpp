#define DOCTEST_CONFIG_IMPLEMENT

#include "public/real_gmock_mocks.hpp"

#include <doctest/doctest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

TEST_CASE("generated gmock mock is usable from doctest") {
    using fixture::third_party::Calculator;
    using fixture::third_party::mocks::CalculatorMock;

    CalculatorMock mock;
    Calculator    *calculator = &mock;

    EXPECT_CALL(mock, add(4, 6)).WillOnce(::testing::Return(10));
    CHECK(calculator->add(4, 6) == 10);

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
