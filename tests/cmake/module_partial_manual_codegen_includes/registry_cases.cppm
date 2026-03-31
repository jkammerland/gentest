module;

#include "gentest/mock_registry_codegen.h"

export module gentest.partial_manual_registry;

import gentest;

export [[using gentest: test("partial_manual/registry_only")]]
void registry_only() {}
