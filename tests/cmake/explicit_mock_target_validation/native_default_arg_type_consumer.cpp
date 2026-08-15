#include "native_default_arg_type_consumer.hpp"

#include "gentest/assertions.h"
#include "public/native_default_arg_type.hpp"

using namespace gentest::asserts;

void native_default_arg_type() {
    fixture::validation::mocks::TypeDefaultArgServiceMock mock;

    gentest::expect(mock, &fixture::validation::TypeDefaultArgService::value)
        .times(1)
        .with(fixture::validation::DefaultArgOptions{})
        .returns(17);
    EXPECT_EQ(mock.value(), 17);
}
