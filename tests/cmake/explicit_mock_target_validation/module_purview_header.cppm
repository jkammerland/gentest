export module fixture.validation_purview_header_defs;

export import gentest.mock;

// The mocked type comes from an ordinary header included in this module's purview.
export {
#include "module_purview_header_service.hpp"
}

export namespace fixture::mocks {
using PurviewServiceMock = gentest::mock<fixture::PurviewService>;
} // namespace fixture::mocks
