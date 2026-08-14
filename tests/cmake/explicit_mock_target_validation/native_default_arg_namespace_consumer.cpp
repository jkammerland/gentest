#include "native_default_arg_namespace_consumer.hpp"

#include "gentest/assertions.h"
#include "public/native_default_arg_namespace.hpp"

using namespace gentest::asserts;

void native_default_arg_namespace() {
    fixture::validation::mocks::NamespaceDefaultArgServiceMock mock;

    EXPECT_CALL(mock, value).times(1).with(fixture::validation::kNamespaceDefaultValue).returns(42);
    EXPECT_EQ(mock.value(), 42);
}
