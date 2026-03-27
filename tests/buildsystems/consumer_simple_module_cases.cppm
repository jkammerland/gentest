export module gentest.consumer_simple_cases;

import gentest.consumer_simple_mocks;

export namespace consumer_simple {

[[using gentest: test("consumer/simple_module_mock_smoke")]]
void simple_module_mock_smoke() {
    consumer_simple::mocks::ServiceMock service;
    (void)service;
}

} // namespace consumer_simple
