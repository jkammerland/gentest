export module gentest.multiline_mock_defs;

// clang-format off
export import
    gentest.mock;
// clang-format on

export import gentest.multiline_provider;

export namespace multiline::mocks {

using ServiceMock = gentest::mock<multiline::provider::Service>;

} // namespace multiline::mocks
