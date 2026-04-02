#pragma once

#include <list>
#include <tuple>
#include <vector>

namespace fixture {

struct TemplateTemplatePacker {
    template <template <class...> class... Cs>
    void join(const std::tuple<Cs<int>...>& value) { (void)value; }
};

} // namespace fixture
