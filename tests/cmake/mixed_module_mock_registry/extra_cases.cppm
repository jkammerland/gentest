module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.mixed_module_extra_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace extramod {

struct Worker {
    virtual ~Worker()               = default;
    virtual int run(int iterations) = 0;
};

} // namespace extramod

export namespace extramod {

[[using gentest: test("mixed/extra_module_mock")]]
void extra_module_mock() {
    gentest::mock<Worker> worker;
    gentest::expect(worker, &Worker::run).times(1).with(5).returns(8);

#if !defined(GENTEST_CODEGEN)
    Worker &base = worker;
    gentest::asserts::EXPECT_EQ(base.run(5), 8);
#endif
}

} // namespace extramod
