#include "public/template_template_pack_direct_expect_mocks.hpp"

#include <list>
#include <tuple>
#include <vector>

int main() {
    gentest::mock<fixture::TemplateTemplatePacker> mock_packer;
    int                                            calls = 0;

    gentest::expect<&fixture::TemplateTemplatePacker::template join<std::vector, std::list>>(
        mock_packer, "::fixture::TemplateTemplatePacker::join<std::vector, std::list>")
        .times(1)
        .invokes([&](const std::tuple<std::vector<int>, std::list<int>> &value) {
            calls += static_cast<int>(std::get<0>(value).size() + std::get<1>(value).size());
        });

    const auto value = std::tuple{std::vector<int>{1, 2}, std::list<int>{3}};
    mock_packer.template join<std::vector, std::list>(value);

    return calls == 3 ? 0 : 1;
}
