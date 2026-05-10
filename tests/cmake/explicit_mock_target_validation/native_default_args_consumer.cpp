#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "public/native_default_args.hpp"

using namespace gentest::asserts;

[[using gentest: test("mock/native_default_args")]]
void native_default_args() {
    fixture::validation::mocks::DefaultArgsServiceMock mock;
    gentest::expect(mock, &fixture::validation::DefaultArgsService::compute).times(1).with(5, 3).returns(8);
    EXPECT_EQ(mock.compute(5), 8);

    fixture::validation::mocks::CtorDefaultArgsServiceMock ctor_mock{6};
    fixture::validation::CtorDefaultArgsService           *service = &ctor_mock;
    EXPECT_EQ(service->seed, 12);
    gentest::expect(ctor_mock, &fixture::validation::CtorDefaultArgsService::load).times(1).with(4).returns(16);
    EXPECT_EQ(ctor_mock.load(), 16);
}
