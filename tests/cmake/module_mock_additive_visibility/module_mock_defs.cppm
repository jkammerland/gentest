export module gentest.additive_provider_mock_defs;

export import gentest.mock;

export import gentest.additive_provider;

export namespace provider::mocks {

using ServiceMock = gentest::mock<provider::Service>;

} // namespace provider::mocks
