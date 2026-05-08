#define DOCTEST_CONFIG_IMPLEMENT

#include "public/real_gmock_mocks.hpp"

#include <doctest/doctest.h>
#include <gmock/gmock.h>
#include <string>

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

TEST_CASE("generated gmock mock is usable from doctest") {
    fixture::third_party::mocks::CalculatorMock mock;
    fixture::third_party::Calculator           *calculator = &mock;

    EXPECT_CALL(mock, add(4, 6)).WillOnce(::testing::Return(10));
    CHECK(calculator->add(4, 6) == 10);

    EXPECT_CALL(mock, name()).WillOnce(::testing::Return(std::string{"doctest"}));
    CHECK(calculator->name() == "doctest");
}
