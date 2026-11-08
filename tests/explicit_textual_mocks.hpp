#pragma once

#include "consumer/service.hpp"
#include "mocking/colliding_namespaces.h"
#include "mocking/types.h"

// Codegen reads the members of an implicitly instantiated class template
// specialization only once the specialization has been instantiated.
static_assert(sizeof(mocking::Store<::detail::Handle>) != 0);

namespace gentest::test_mocks {

using ConsumerServiceMock            = gentest::mock<consumer::Service>;
using CalculatorMock                 = gentest::mock<mocking::Calculator>;
using RefProviderMock                = gentest::mock<mocking::RefProvider>;
using TickerMock                     = gentest::mock<mocking::Ticker>;
using NoDefaultMock                  = gentest::mock<mocking::NoDefault>;
using NeedsInitMock                  = gentest::mock<mocking::NeedsInit>;
using MoveOnlyConsumerMock           = gentest::mock<mocking::MOConsumer>;
using ForwardingAliasMock            = gentest::mock<mocking::ForwardingAlias>;
using TemplateTemplateFixedMock      = gentest::mock<mocking::TemplateTemplateFixed>;
using TemplateTemplateCtorTargetMock = gentest::mock<mocking::TemplateTemplateCtorTarget>;
using TemplateTemplatePackerMock     = gentest::mock<mocking::TemplateTemplatePacker>;
using RefWrapConsumerMock            = gentest::mock<mocking::RefWrapConsumer>;
using StringerMock                   = gentest::mock<mocking::Stringer>;
using FloaterMock                    = gentest::mock<mocking::Floater>;
using DerivedRunnerMock              = gentest::mock<mocking::DerivedRunner>;
using CollidingNamespacesMock        = gentest::mock<mocking::CollidingNamespaces>;
using CollidingStoreMock             = gentest::mock<mocking::Store<::detail::Handle>>;

} // namespace gentest::test_mocks
