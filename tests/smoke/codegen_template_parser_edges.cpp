#include "gentest/attributes.h"

#include <array>
#include <list>
#include <type_traits>
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

template <typename TQuotedComma, typename TEscapedQuote, typename TQualified>
[[using gentest: test("smoke/template_parser_edges/syntax_sensitive"), template(TQuotedComma, decltype("a,b")),
  template(TEscapedQuote, decltype("\"edge\"")), template(TQualified, std::integral_constant<int, 1> const)]]
void template_parser_syntax_sensitive() {}

template <char8_t C8, char16_t C16, char32_t C32, wchar_t CW>
[[using gentest: test("smoke/template_parser_edges/char_prefixes"), template(C8, u8'c'), template(C16, u'd'), template(C32, U'e'),
  template(CW, L'f')]]
void template_parser_char_prefixes() {}
