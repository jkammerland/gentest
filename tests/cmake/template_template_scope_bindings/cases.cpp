#include "gentest/runner.h"

#include <list>
#include <vector>

namespace template_scope {

template <typename T>
using VecAlias = std::vector<T>;

enum class Flag {
    A,
    B,
};

namespace nested {

template <template <class...> class C>
[[using gentest: test("template_scope/nested_unqualified_alias"), template(C, VecAlias, std::list)]]
void nested_unqualified_alias() {
    C<int> values;
    values.emplace_back(1);
    gentest::expect_eq(values.size(), std::size_t{1}, "nested unqualified alias binding");
}

template <Flag F>
[[using gentest: test("template_scope/nested_unqualified_parenthesized_enum"), template(F, (Flag::A))]]
void nested_unqualified_parenthesized_enum() {
    static_assert(F == Flag::A);
}

} // namespace nested

} // namespace template_scope
