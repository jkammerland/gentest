export module downstream.xrepo.mock_defs;

export import gentest.mock;
export import downstream.xrepo.service;

export namespace downstream::mocks {

using ServiceMock = gentest::mock<downstream::Service>;

} // namespace downstream::mocks
