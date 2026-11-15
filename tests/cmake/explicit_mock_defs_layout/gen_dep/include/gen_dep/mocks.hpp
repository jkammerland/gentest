#pragma once

#include "gentest/mock_fwd.h"

// Written by a custom command of the linked 'gen_dep_api' library.
#include <api_gen.hpp>

namespace layout_gen_dep::mocks {

using ApiMock = gentest::mock<layout_gen_dep::Api>;

} // namespace layout_gen_dep::mocks
