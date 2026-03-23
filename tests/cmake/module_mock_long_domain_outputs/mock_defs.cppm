export module gentest.long_domain_mock_defs;

import gentest.mock;

export import gentest.module_mock_domain_name_is_far_beyond_thirty_two_chars_provider;

export namespace long_domain::mocks {

using ServiceMock = gentest::mock<long_domain::Service>;

} // namespace long_domain::mocks
