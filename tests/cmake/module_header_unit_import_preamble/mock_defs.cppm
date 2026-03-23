export module gentest.header_unit_import_preamble_mock_defs;

import gentest.mock;

export import gentest.header_unit_import_preamble_provider;

export namespace header_unit_proof::mocks {

using ServiceMock = gentest::mock<header_unit_proof::Service>;

} // namespace header_unit_proof::mocks
