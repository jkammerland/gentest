module;

#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE

export module gentest.mock;

export namespace gentest {

using ::gentest::expect;
using ::gentest::make_nice;
using ::gentest::make_strict;
using ::gentest::mock;

namespace detail {
using ::gentest::detail::MockAccess;

namespace mocking {
using ::gentest::detail::mocking::ExpectationHandle;
using ::gentest::detail::mocking::ExpectationPusher;
using ::gentest::detail::mocking::InstanceState;
using ::gentest::detail::mocking::MethodTraits;
}
} // namespace detail

namespace match {
using ::gentest::match::AllOf;
using ::gentest::match::Any;
using ::gentest::match::AnyOf;
using ::gentest::match::EndsWith;
using ::gentest::match::Eq;
using ::gentest::match::Ge;
using ::gentest::match::Gt;
using ::gentest::match::InRange;
using ::gentest::match::Le;
using ::gentest::match::Lt;
using ::gentest::match::Near;
using ::gentest::match::Not;
using ::gentest::match::StartsWith;
using ::gentest::match::StrContains;
}

} // namespace gentest
