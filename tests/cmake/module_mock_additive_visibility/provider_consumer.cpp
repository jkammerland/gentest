#include "consumer_api.hpp"

import gentest.additive_provider_mocks;

auto run_provider_self() -> bool {
    provider::mocks::ServiceMock service;
    gentest::expect(service, &provider::Service::compute).times(1).with(3).returns(5);

    provider::Service &base = service;
    return base.compute(3) == 5;
}
