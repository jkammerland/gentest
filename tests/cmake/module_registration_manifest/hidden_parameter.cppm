module;

export module gentest.story034.hidden_parameter;

import gentest;

namespace story034_hidden_parameter {

namespace detail {

inline constexpr int hidden_value = 7;

} // namespace detail

export [[using gentest: test("module_registration/hidden_parameter"), parameters(value, detail::hidden_value)]]
void hidden_parameter(int value) {
    gentest::asserts::EXPECT_EQ(value, 7);
}

} // namespace story034_hidden_parameter
