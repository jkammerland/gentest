#include "gentest/runner.h"

#include <list>
#include <type_traits>
#include <vector>

namespace template_scope {

template <typename T> using VecAlias = std::vector<T>;

enum class Flag {
    A,
    B,
};

template <typename T> using ShadowVec = std::vector<T>;

enum class ShadowFlag {
    OuterA,
    OuterB,
};

namespace nested {

template <template <class...> class C>
[[using gentest: test("unqualified_alias"), template(C, VecAlias, std::list)]]
void nested_unqualified_alias() {
    C<int> values;
    values.emplace_back(1);
    gentest::expect_eq(values.size(), std::size_t{1}, "nested unqualified alias binding");
}

template <Flag F>
[[using gentest: test("unqualified_parenthesized_enum"), template(F, (Flag::A))]]
void nested_unqualified_parenthesized_enum() {
    static_assert(F == Flag::A);
}

template <typename T> using ShadowVec = std::list<T>;

enum class ShadowFlag {
    A,
    B,
};

template <template <class...> class C>
[[using gentest: test("shadowed_alias"), template(C, ShadowVec)]]
void shadowed_alias() {
    static_assert(std::is_same_v<C<int>, std::list<int>>);
}

template <ShadowFlag F>
[[using gentest: test("shadowed_enum"), template(F, (ShadowFlag::A))]]
void shadowed_enum() {
    static_assert(F == ShadowFlag::A);
}

} // namespace nested

} // namespace template_scope
