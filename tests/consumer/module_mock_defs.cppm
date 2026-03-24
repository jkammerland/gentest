export module gentest.consumer_mock_defs;

export import gentest.mock;

export import gentest.consumer_service;

export namespace consumer::mocks {

using ServiceMock = gentest::mock<consumer::Service>;

} // namespace consumer::mocks
