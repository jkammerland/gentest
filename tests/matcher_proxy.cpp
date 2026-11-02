#include "gentest/matchers.h"

#include <array>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>

namespace proxy_support {

struct Common {
    const int *value;
};

struct Value {
    std::unique_ptr<int> value;
                         operator Common() const { return {value.get()}; }
};

struct Reference {
    std::unique_ptr<int> &value;
                          operator Common() const { return {value.get()}; }
};

} // namespace proxy_support

namespace std {
template <template <class> class Q1, template <class> class Q2>
struct basic_common_reference<proxy_support::Reference, proxy_support::Value, Q1, Q2> {
    using type = proxy_support::Common;
};
template <template <class> class Q1, template <class> class Q2>
struct basic_common_reference<proxy_support::Value, proxy_support::Reference, Q1, Q2> {
    using type = proxy_support::Common;
};
} // namespace std

namespace proxy_support {

struct Iterator {
    using value_type       = Value;
    using difference_type  = std::ptrdiff_t;
    using iterator_concept = std::forward_iterator_tag;

    Value    *position;
    Reference operator*() const { return {position->value}; }
    Iterator &operator++() {
        ++position;
        return *this;
    }
    Iterator operator++(int) {
        auto previous = *this;
        ++*this;
        return previous;
    }
    bool operator==(const Iterator &) const = default;
};

struct Range {
    Value   *first;
    Value   *last;
    Iterator begin() const { return {first}; }
    Iterator end() const { return {last}; }
};

static_assert(std::ranges::forward_range<const Range>);
static_assert(!std::is_copy_constructible_v<std::ranges::range_value_t<const Range>>);

} // namespace proxy_support

int main() {
    std::array<proxy_support::Value, 2> values{{{std::make_unique<int>(7)}, {std::make_unique<int>(9)}}};
    const proxy_support::Range          range{.first = values.data(), .last = values.data() + values.size()};
    int                                 calls   = 0;
    const auto                          matcher = gentest::match::ElementsAre(
                                                      [&](const auto &item) {
                                 ++calls;
                                 return item.value && *item.value == 7;
                                                      },
                                                      [&](const auto &item) {
                                 ++calls;
                                 return item.value && *item.value == 9;
                                                      })
                                                      .make<proxy_support::Range>();
    const auto                          result  = matcher.evaluate(range);
    return result.matched && calls == 2 && values[0].value && values[1].value ? 0 : 1;
}
