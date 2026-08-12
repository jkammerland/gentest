export module downstream.bazel.mock_defs;

export import gentest.mock;
export import downstream.bazel.service;

export namespace downstream::mocks {

using ServiceMock = gentest::mock<downstream::Service>;

} // namespace downstream::mocks
