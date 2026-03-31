#include "gentest/runner.h"
#include "public/defaulted_template_ctor_mocks.hpp"

[[using gentest: test("fixture/mock_defaulted_template_ctor/compiles")]]
void mock_defaulted_template_ctor_compiles() {
    gentest::mock<fixture::DefaultedTemplateCtorTarget> mock_target{std::array<int, 2>{1, 2}};
    EXPECT_CALL(mock_target, ping).times(1);

    mock_target.ping();
}
