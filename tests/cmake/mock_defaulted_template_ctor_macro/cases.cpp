#include "gentest/runner.h"
#include "public/defaulted_template_ctor_macro_mocks.hpp"

[[using gentest: test("fixture/mock_defaulted_template_ctor_macro/compiles")]]
void mock_defaulted_template_ctor_macro_compiles() {
    gentest::mock<fixture::MacroDefaultedTemplateCtorTarget> mock_target{std::array<int, 2>{1, 2}};
    EXPECT_CALL(mock_target, ping).times(1);

    mock_target.ping();
}
