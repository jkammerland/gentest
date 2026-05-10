#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "public/native_generic_alias.hpp"

using namespace gentest::asserts;

[[using gentest: test("mock/native_generic_alias_template")]]
void native_generic_alias_template() {
    fixture::validation::mocks::GenericAliasServiceMock mock;

    EXPECT_CALL(mock, value).times(1).returns(19);
    fixture::validation::GenericAliasService *service = &mock;
    EXPECT_EQ(service->value(), 19);
}
