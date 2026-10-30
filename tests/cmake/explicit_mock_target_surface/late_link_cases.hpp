#pragma once

#include "gentest/attributes.h"

[[using gentest: test("explicit_mock_target/late_link_consumer")]]
void late_link_consumer();

#if defined(GENTEST_EXPLICIT_MOCK_COMMAND_PROBE)
[[using gentest: test("explicit_mock_target/compile_command_change")]]
void compile_command_change();
#endif
