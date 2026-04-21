module;

export module gentest.story035.mock_split_provider;

import gentest.mock;

export namespace story035_mock_split {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

using ServiceMock = gentest::mock<Service>;

[[using gentest: test("module_registration_mock_split/module_owned_mock")]]
void module_owned_mock(ServiceMock &service) {
    (void)&service;
}

} // namespace story035_mock_split
