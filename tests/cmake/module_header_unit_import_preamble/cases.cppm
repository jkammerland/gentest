module;

export module gentest.header_unit_import_preamble;

import <vector>;
import gentest;
import gentest.header_unit_import_preamble_mocks;

using namespace gentest::asserts;

export namespace header_unit_proof {

} // namespace header_unit_proof

export [[using gentest: test("header_unit/import_preamble")]]
void header_unit_import_preamble() {
    std::vector<int> inputs{4};
    header_unit_proof::mocks::ServiceMock service;
    gentest::expect(service, &header_unit_proof::Service::compute).times(1).with(inputs.front()).returns(9);
    header_unit_proof::Service &base = service;
    EXPECT_EQ(base.compute(inputs.front()), 9);
}
