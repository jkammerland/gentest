#include "consumer_api.hpp"
#include "public/additive_header_mocks.hpp"

auto run_header_defined_from_module() -> bool {
    shared::mocks::ServiceMock service;
    gentest::expect(service, &shared::Service::compute).times(1).with(4).returns(9);

    shared::Service &base = service;
    return base.compute(4) == 9;
}
