#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "public/native_inherited_final.hpp"

using namespace gentest::asserts;

[[using gentest: test("mock/native_inherited_final")]]
void native_inherited_final() {
    fixture::validation::mocks::InheritedFinalServiceMock mock;
    fixture::validation::InheritedFinalBase              *base = &mock;

    EXPECT_EQ(base->inherited_final(), 11);
    EXPECT_CALL(mock, visible).times(1).returns(13);
    EXPECT_EQ(mock.visible(), 13);
}
