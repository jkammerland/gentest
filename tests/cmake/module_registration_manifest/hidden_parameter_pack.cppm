module;

export module gentest.story034.hidden_parameter_pack;

import gentest;

namespace story034_hidden_parameter_pack {

namespace detail {

inline constexpr int first  = 1;
inline constexpr int second = 2;

} // namespace detail

export [[using gentest: test("module_registration/hidden_parameter_pack"), parameters_pack((lhs, rhs), (detail::first, detail::second))]]
void hidden_parameter_pack(int lhs, int rhs) {
    gentest::asserts::EXPECT_EQ(lhs + rhs, 3);
}

} // namespace story034_hidden_parameter_pack
