module;

export module gentest.story035.mock_split_cases;

import gentest.story035.mock_split_provider;

namespace gentest {
template <typename T> struct mock;
}

namespace story035_mock_split {

// This source-level mock use drives the split inspect-mocks phase. The
// generated specialization is compiled in the same-module registration unit
// inspected by the regression script below.
using ServiceMock = gentest::mock<Service>;

[[using gentest: test("module_registration_mock_split/module_owned_mock")]]
void module_owned_mock() {
    ServiceMock *service = nullptr;
    (void)service;
}

} // namespace story035_mock_split
