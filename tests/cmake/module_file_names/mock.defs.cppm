// The extra dot in this file name is the point of the fixture: gentest must
// still treat it as a module unit.
export module module_file_names.mock_defs;

import gentest.mock;

export import module_file_names.service;

export namespace module_file_names::mocks {
using ServiceMock = gentest::mock<module_file_names::Service>;
}
