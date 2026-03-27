module;

#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE

export module gentest.consumer_simple_mock_defs;

export namespace consumer_simple {

struct Service {
    virtual ~Service()            = default;
    virtual int compute(int arg) = 0;
};

namespace mocks {

using ServiceMock = gentest::mock<Service>;

} // namespace mocks

} // namespace consumer_simple
