#include "public/real_gmock_mocks.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

namespace {

TEST(GeneratedGMockConsumer, UsesGeneratedMockClass) {
    fixture::third_party::mocks::CalculatorMock mock;
    fixture::third_party::Calculator           *calculator = &mock;

    EXPECT_CALL(mock, add(2, 3)).WillOnce(::testing::Return(5));
    EXPECT_EQ(calculator->add(2, 3), 5);

    EXPECT_CALL(mock, name()).WillOnce(::testing::Return(std::string{"generated"}));
    EXPECT_EQ(calculator->name(), "generated");
}

} // namespace
