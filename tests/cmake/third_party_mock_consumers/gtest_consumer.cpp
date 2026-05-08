#include "public/real_gmock_mocks.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

TEST(GeneratedGMockConsumer, UsesGeneratedMockClass) {
    using fixture::third_party::Calculator;
    using fixture::third_party::mocks::CalculatorMock;

    CalculatorMock mock;
    Calculator    *calculator = &mock;

    EXPECT_CALL(mock, add(2, 3)).WillOnce(::testing::Return(5));
    EXPECT_EQ(calculator->add(2, 3), 5);

    EXPECT_CALL(mock, name()).WillOnce(::testing::Return(std::string{"generated"}));
    EXPECT_EQ(calculator->name(), "generated");
}

TEST(GeneratedGMockConsumer, HandlesIndirectTemplateTypes) {
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

    EXPECT_CALL(mock, make("unit")).WillOnce(Return(ByMove(std::make_unique<int>(42))));
    auto owned = factory->make("unit");
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(*owned, 42);

    int stored = 17;
    EXPECT_CALL(mock, value()).WillOnce(ReturnRef(stored));
    EXPECT_EQ(&factory->value(), &stored);

    EXPECT_CALL(mock, combine(ElementsAre(IntPair{1, 2}, IntPair{3, 4}))).WillOnce(Return(IntPair{4, 6}));
    EXPECT_EQ(factory->combine(PairBatch{IntPair{1, 2}, IntPair{3, 4}}), (IntPair{4, 6}));

    EXPECT_CALL(mock, consume(Pointee(9))).WillOnce(Return(true));
    EXPECT_TRUE(factory->consume(std::make_unique<int>(9)));
}

} // namespace
