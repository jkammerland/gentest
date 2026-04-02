#include "public/template_template_ctor_traits_mocks.hpp"

#include <array>
#include <type_traits>

static_assert(std::is_nothrow_constructible_v<gentest::mock<fixture::TemplateTemplateCtorTarget>, std::array<int, 2>>);

int main() {
    gentest::mock<fixture::TemplateTemplateCtorTarget> mock_target{std::array<int, 2>{1, 2}};
    mock_target.ping();
    return 0;
}
