#include "dual_target.hpp"

#include <gentest/runner.h>

namespace header_declaration_registration {

void dual_target_case() { gentest::expect(DUAL_TARGET_VARIANT == 1 || DUAL_TARGET_VARIANT == 2); }

} // namespace header_declaration_registration
