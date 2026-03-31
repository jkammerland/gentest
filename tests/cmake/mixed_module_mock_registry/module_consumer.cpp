#include "consumer_api.hpp"

import gentest.mixed_module_mocks;

auto run_module_mocks() -> bool {
    mixmod::mocks::ServiceMock module_service;
    extramod::mocks::WorkerMock extra_worker;
    sameblock::mocks::ServiceMock same_block_service;

    gentest::expect(module_service, &mixmod::Service::compute).times(1).with(6).returns(10);
    gentest::expect(extra_worker, &extramod::Worker::run).times(1).with(3).returns(12);
    gentest::expect(same_block_service, &sameblock::Service::compute).times(1).with(9).returns(14);

    mixmod::Service &module_base = module_service;
    extramod::Worker &extra_base = extra_worker;
    sameblock::Service &same_block_base = same_block_service;
    return module_base.compute(6) == 10 && extra_base.run(3) == 12 && same_block_base.compute(9) == 14;
}
