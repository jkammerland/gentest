#include "mock_cases.hpp"

#include <gentest/mock.h>

namespace header_declaration_registration {

void explicit_mock_case() {
    MockServiceDouble mock_service;
    gentest::expect(mock_service, &MockService::compute).times(1).with(7).returns(49);
    MockService *service = &mock_service;
    gentest::expect_eq(service->compute(7), 49);
}

} // namespace header_declaration_registration
