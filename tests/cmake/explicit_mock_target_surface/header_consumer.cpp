#include "public/fixture_header_mocks.hpp"
#include "public/fixture_alt_header_mocks.hpp"

int main() {
    fixture::mocks::ServiceMock alias_mock;
    gentest::expect(alias_mock, &fixture::Service::compute).times(1).with(3).returns(fixture::kServiceSentinel);
    fixture::Service *service = &alias_mock;
    if (service->compute(3) != fixture::kServiceSentinel) {
        return 1;
    }

    gentest::mock<fixture::Service> raw_mock;
    gentest::expect(raw_mock, &fixture::Service::compute).times(1).with(4).returns(7);
    fixture::Service *raw_service = &raw_mock;
    if (raw_service->compute(4) != 7) {
        return 2;
    }

    fixture::mocks::AltServiceMock alt_alias_mock;
    gentest::expect(alt_alias_mock, &fixture::AltService::scale).times(1).with(5).returns(15);
    fixture::AltService *alt_service = &alt_alias_mock;
    if (alt_service->scale(5) != 15) {
        return 3;
    }

    gentest::mock<fixture::AltService> alt_raw_mock;
    gentest::expect(alt_raw_mock, &fixture::AltService::scale).times(1).with(2).returns(8);
    fixture::AltService *alt_raw_service = &alt_raw_mock;
    if (alt_raw_service->scale(2) != 8) {
        return 4;
    }

    return 0;
}
