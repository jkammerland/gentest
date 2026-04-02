module;

export module gentest.multi_imported_sibling_consumer;

import gentest;
import gentest.multi_imported_sibling_mocks;

using namespace gentest::asserts;

export namespace multi_imported_sibling {

[[using gentest: test("multi_imported_sibling/module_two_module_mocks")]]
void module_two_module_mocks() {
    multi_imported_sibling::mocks::AlphaServiceMock alpha_service;
    multi_imported_sibling::mocks::BetaServiceMock  beta_service;

    gentest::expect(alpha_service, &multi_imported_sibling::alpha::Service::compute_alpha).times(1).with(8).returns(21);
    gentest::expect(beta_service, &multi_imported_sibling::beta::Service::compute_beta).times(1).with(5).returns(13);

    multi_imported_sibling::alpha::Service &alpha_base = alpha_service;
    multi_imported_sibling::beta::Service  &beta_base  = beta_service;
    EXPECT_EQ(alpha_base.compute_alpha(8), 21);
    EXPECT_EQ(beta_base.compute_beta(5), 13);
}

} // namespace multi_imported_sibling
