module;

export module gentest.multiline_consumer;

import gentest.multiline_provider;
import gentest;
import gentest.multiline_mocks;

using namespace gentest::asserts;

export namespace multiline {

[[using gentest: test("multiline/module_mock")]]
void module_mock() {
    multiline::mocks::ServiceMock service;
    gentest::expect(service, &multiline::provider::Service::compute).times(1).with(5).returns(13);
    multiline::provider::Service &base = service;
    EXPECT_EQ(base.compute(5), 13);
}

} // namespace multiline
