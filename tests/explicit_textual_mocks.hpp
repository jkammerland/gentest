#pragma once

#include "consumer/service.hpp"
#include "mocking/types.h"

namespace gentest::test_mocks {

using ConsumerServiceMock = gentest::mock<consumer::Service>;
using CalculatorMock = gentest::mock<mocking::Calculator>;
using RefProviderMock = gentest::mock<mocking::RefProvider>;
using TickerMock = gentest::mock<mocking::Ticker>;
using NoDefaultMock = gentest::mock<mocking::NoDefault>;
using NeedsInitMock = gentest::mock<mocking::NeedsInit>;
using MoveOnlyConsumerMock = gentest::mock<mocking::MOConsumer>;
using ForwardingAliasMock = gentest::mock<mocking::ForwardingAlias>;
using RefWrapConsumerMock = gentest::mock<mocking::RefWrapConsumer>;
using StringerMock = gentest::mock<mocking::Stringer>;
using FloaterMock = gentest::mock<mocking::Floater>;
using DerivedRunnerMock = gentest::mock<mocking::DerivedRunner>;

} // namespace gentest::test_mocks
