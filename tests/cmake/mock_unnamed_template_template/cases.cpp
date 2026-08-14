#include "cases.hpp"

#include "gentest/runner.h"
#include "public/mock_unnamed_template_template_mocks.hpp"

namespace fixture {

void mock_unnamed_template_template_compiles() {
    gentest::mock<UnnamedTemplateTemplateMethodTarget> mock_target;
    mock_target.template take<std::array>(std::array<int, 2>{1, 2});
}

} // namespace fixture
