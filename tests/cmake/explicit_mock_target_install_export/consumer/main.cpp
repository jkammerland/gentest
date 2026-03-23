#include "public/fixture_header_mocks.hpp"

int main() {
    fixture::mocks::ServiceMock alias_mock;
    gentest::expect(alias_mock, &fixture::Service::compute).times(1).with(8).returns(16);
    fixture::Service *alias_service = &alias_mock;
    if (alias_service->compute(8) != 16) {
        return 1;
    }

    gentest::mock<fixture::Service> raw_mock;
    gentest::expect(raw_mock, &fixture::Service::compute).times(1).with(5).returns(25);
    fixture::Service *raw_service = &raw_mock;
    if (raw_service->compute(5) != 25) {
        return 2;
    }

    return 0;
}
