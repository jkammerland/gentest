#include "consumer_api.hpp"
#include "public/mixed_legacy_mocks.hpp"

auto run_legacy_mock() -> bool {
    legacy::mocks::ServiceMock service;
    gentest::expect(service, &legacy::Service::compute).times(1).with(4).returns(6);

    legacy::Service &base = service;
    return base.compute(4) == 6;
}
