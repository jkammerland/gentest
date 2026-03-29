module;

#include "gentest/mock_registry_codegen.h"

export module gentest.manual_partial_registry;

export [[using gentest: test("partial/manual_registry_include")]]
void manual_registry_include() {}
