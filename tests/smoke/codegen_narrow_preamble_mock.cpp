#include "codegen_narrow_preamble_mock_service.hpp"
#include "gentest/mock.h"

namespace smoke::narrow_preamble {

using ServiceMock = gentest::mock<Service>;

[[using gentest: test("narrow/full/mock")]]
void mock_case() {}

} // namespace smoke::narrow_preamble
