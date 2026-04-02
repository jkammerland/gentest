export module gentest.multi_imported_sibling_mock_defs;

export import gentest.mock;

export import gentest.multi_imported_sibling_provider_alpha;
export import gentest.multi_imported_sibling_provider_beta;

export namespace multi_imported_sibling::mocks {

using AlphaServiceMock = gentest::mock<multi_imported_sibling::alpha::Service>;
using BetaServiceMock  = gentest::mock<multi_imported_sibling::beta::Service>;

} // namespace multi_imported_sibling::mocks
