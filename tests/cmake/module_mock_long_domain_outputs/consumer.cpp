import gentest.long_domain_mocks;

auto run_provider_self() -> bool {
    long_domain::mocks::ServiceMock service;
    gentest::expect(service, &long_domain::Service::compute).times(1).with(7).returns(11);

    long_domain::Service &base = service;
    return base.compute(7) == 11;
}
