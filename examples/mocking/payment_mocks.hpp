#pragma once

#include "gentest/mock_fwd.h"
#include "payment.hpp"

namespace shop::mocks {
using PaymentGatewayMock = gentest::mock<shop::PaymentGateway>;
} // namespace shop::mocks
