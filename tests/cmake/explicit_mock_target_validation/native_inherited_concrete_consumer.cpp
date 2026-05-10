#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "public/native_inherited_concrete.hpp"

using namespace gentest::asserts;

[[using gentest: test("mock/native_inherited_concrete")]]
void native_inherited_concrete() {
    fixture::validation::mocks::InheritedConcreteServiceMock mock;
    fixture::validation::InheritedConcreteBase              *base = &mock;

    gentest::expect(mock, &fixture::validation::InheritedConcreteBase::inherited).times(1).with(5).returns(50);
    gentest::expect<&fixture::validation::InheritedConcreteService::visible>(mock,
                                                                             "::fixture::validation::InheritedConcreteService::visible")
        .times(1)
        .with(7)
        .returns(70);

    EXPECT_EQ(base->inherited(5), 50);
    EXPECT_EQ(mock.visible(7), 70);
}
