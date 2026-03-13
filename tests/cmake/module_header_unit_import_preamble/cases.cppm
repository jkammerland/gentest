module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.header_unit_import_preamble;

#if !defined(GENTEST_CODEGEN)
import <vector>;
#endif

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace header_unit_proof {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace header_unit_proof

export [[using gentest: test("header_unit/import_preamble")]]
void header_unit_import_preamble() {
    std::vector<int> inputs{4};
    gentest::mock<header_unit_proof::Service> service;
    gentest::expect(service, &header_unit_proof::Service::compute).times(1).with(inputs.front()).returns(9);

#if !defined(GENTEST_CODEGEN)
    header_unit_proof::Service &base = service;
    EXPECT_EQ(base.compute(inputs.front()), 9);
#endif
}
