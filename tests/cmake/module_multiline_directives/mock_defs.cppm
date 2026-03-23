export module gentest.multiline_mock_defs;

import gentest.mock;

export import gentest.multiline_provider;

export namespace multiline::mocks {

using ServiceMock = gentest::mock<multiline::provider::Service>;

} // namespace multiline::mocks
