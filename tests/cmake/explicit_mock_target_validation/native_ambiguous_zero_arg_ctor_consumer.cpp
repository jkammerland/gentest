#include "native_ambiguous_zero_arg_ctor_consumer.hpp"

#include "gentest/assertions.h"
#include "public/native_ambiguous_zero_arg_ctor.hpp"

using namespace gentest::asserts;

void native_ambiguous_zero_arg_ctor() {
    fixture::validation::mocks::AmbiguousZeroArgCtorServiceMock mock{5};

    EXPECT_EQ(mock.seed, 5);
    gentest::expect(mock, &fixture::validation::AmbiguousZeroArgCtorService::load).times(1).returns(8);
    EXPECT_EQ(mock.load(), 8);
}
