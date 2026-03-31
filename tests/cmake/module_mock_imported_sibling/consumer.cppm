module;

export /* consumer */ module gentest.imported_sibling_consumer;

import gentest;
import gentest.imported_sibling_mocks;

using namespace gentest::asserts;

export namespace imported_sibling {

[[using gentest: test("imported_sibling/module_mock")]]
void module_mock() {
    imported_sibling::mocks::ServiceMock service;
    gentest::expect(service, &imported_sibling::provider::Service::compute).times(1).with(6).returns(15);

    imported_sibling::provider::Service &base = service;
    EXPECT_EQ(base.compute(6), 15);
}

} // namespace imported_sibling
