#include "gentest/runner.h"

#include <type_traits>

namespace member_operator_template_keyword {

struct LessFixture {
    [[using gentest: test("member_operator_less"), parameters(rhs, 7)]]
    bool operator<(int rhs) {
        return rhs == 7;
    }
};

struct TemplateFixture {
    template <typename T>
    [[using gentest: test("member_template"), template(T, int)]]
    void invoke() {
        static_assert(std::is_same_v<T, int>);
    }
};

} // namespace member_operator_template_keyword
