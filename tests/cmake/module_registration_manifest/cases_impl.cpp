module;

#include <coroutine>

module gentest.story034.module_registration;

import gentest;

namespace story034_module_registration {

void exported_redeclaration(Fixture &fixture) { gentest::asserts::EXPECT_EQ(fixture.value, 7); }

gentest::async_test<void> exported_async() { co_return; }

} // namespace story034_module_registration
