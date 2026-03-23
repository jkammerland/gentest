export module gentest.imported_sibling_mock_defs;

export import gentest.imported_sibling_provider;

export namespace imported_sibling::mocks {

using ServiceMock = gentest::mock<imported_sibling::provider::Service>;

} // namespace imported_sibling::mocks
