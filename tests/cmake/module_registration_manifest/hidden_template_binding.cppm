module;

export module gentest.story034.hidden_template_binding;

import gentest;

namespace story034_hidden_template_binding {

namespace detail {

struct Hidden {};

} // namespace detail

export template <typename T>
[[using gentest: test("module_registration/hidden_template_binding"), template(T, detail::Hidden)]]
void hidden_template_binding() {}

} // namespace story034_hidden_template_binding
