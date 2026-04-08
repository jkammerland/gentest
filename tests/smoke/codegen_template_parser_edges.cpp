#include "gentest/attributes.h"

#include <array>
#include <list>
#include <utility>
#include <vector>

template <typename T, char C>
[[using gentest: test("smoke/template_parser_edges/type_char"), template(T, std::pair<int, std::array<long, 2>>),
  template(C, 'a', '\n', '\'')]]
void template_parser_type_char() {}

template <template <class...> class C, typename T>
[[using gentest: test("smoke/template_parser_edges/template_template"), template(C, std::vector, std::list),
  template(T, std::pair<int, std::array<long, 2>>)]]
void template_parser_template_template() {}
