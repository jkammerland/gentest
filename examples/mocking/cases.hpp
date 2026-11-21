#pragma once

#include "gentest/test.h"
#include "public/payment_mocks.hpp"

using namespace gentest::asserts;

namespace mocking {

[[gentest::test("payment_succeeds")]]
inline void paymentSucceeds() {
    shop::mocks::PaymentGatewayMock gateway;
    gentest::expect(gateway, &shop::PaymentGateway::charge).times(1).with(250).returns(true);

    shop::Checkout checkout(gateway);
    EXPECT_TRUE(checkout.pay(250));
}

[[gentest::test("payment_declined")]]
inline void paymentDeclined() {
    shop::mocks::PaymentGatewayMock gateway;
    gentest::expect(gateway, &shop::PaymentGateway::charge).times(1).with(250).returns(false);

    shop::Checkout checkout(gateway);
    EXPECT_FALSE(checkout.pay(250));
}

[[using gentest: test("invalid_amount"), parameters(cents, -1, 0)]]
inline void invalidAmount(int cents) {
    shop::mocks::PaymentGatewayMock gateway;
    gentest::expect(gateway, &shop::PaymentGateway::charge).times(0);

    shop::Checkout checkout(gateway);
    EXPECT_FALSE(checkout.pay(cents));
}

} // namespace mocking
