if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckSyntaxSensitiveRegressionFixtures.cmake: SOURCE_DIR not set")
endif()

function(_gentest_read_fixture file_path out_var)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "Missing syntax-sensitive regression fixture: ${file_path}")
  endif()
  file(READ "${file_path}" _fixture_content)
  set(${out_var} "${_fixture_content}" PARENT_SCOPE)
endfunction()

function(_gentest_require_substring file_path needle description)
  _gentest_read_fixture("${file_path}" _fixture_content)
  string(FIND "${_fixture_content}" "${needle}" _needle_pos)
  if(_needle_pos EQUAL -1)
    message(FATAL_ERROR "${description}: expected to find '${needle}' in ${file_path}")
  endif()
endfunction()

set(_multiline_mock_defs "${SOURCE_DIR}/tests/cmake/module_multiline_directives/mock_defs.cppm")
set(_manual_include_whitespace "${SOURCE_DIR}/tests/cmake/module_manual_include_whitespace/cases.cppm")
set(_install_header_mocks "${SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export/producer/header_mocks.hpp")
set(_install_module_support "${SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export/producer/service_module.cppm")

foreach(_required IN ITEMS
    "${_multiline_mock_defs}"
    "${_manual_include_whitespace}"
    "${_install_header_mocks}"
    "${_install_module_support}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing syntax-sensitive regression fixture: ${_required}")
  endif()
endforeach()

file(READ "${_multiline_mock_defs}" _multiline_content)
foreach(_expected IN ITEMS
    "// clang-format off"
    "// clang-format on")
  string(FIND "${_multiline_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "Module multiline regression fixture lost format guard: ${_expected}")
  endif()
endforeach()
string(REGEX MATCH "export import[ \t\r]*\n[ \t]+gentest\\.mock;" _multiline_match "${_multiline_content}")
if(_multiline_match STREQUAL "")
  message(FATAL_ERROR
    "Module multiline regression fixture must keep `export import` and `gentest.mock;` on separate lines.")
endif()

file(READ "${_manual_include_whitespace}" _manual_include_whitespace_content)
string(FIND "${_manual_include_whitespace_content}" "# include \"public/manual_include_whitespace_mocks.hpp\"" _manual_include_pos)
if(_manual_include_pos EQUAL -1)
  message(FATAL_ERROR
    "Manual include whitespace regression fixture must preserve the spaced `# include` form.")
endif()

file(READ "${_install_header_mocks}" _header_mocks_content)
foreach(_expected IN ITEMS
    "#include <fixture/generated_support.hpp>"
    "#include <fixture/service.hpp>")
  string(FIND "${_header_mocks_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "Explicit mock install/export header fixture lost angle include coverage: ${_expected}")
  endif()
endforeach()

file(READ "${_install_module_support}" _module_support_content)
string(FIND "${_module_support_content}" "#include <fixture/module_support.hpp>" _module_support_pos)
if(_module_support_pos EQUAL -1)
  message(FATAL_ERROR "Explicit mock install/export module fixture lost angle include coverage for fixture/module_support.hpp")
endif()

_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/namespace_suite_comment.cpp"
  "]] // trailing comment"
  "Namespace suite comment regression fixture lost the trailing comment separator")

_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(TQuotedComma, decltype(\"a,b\"))"
  "Template parser edge fixture lost the quoted-comma candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(TEscapedQuote, decltype(\"\\\"edge\\\"\"))"
  "Template parser edge fixture lost the escaped-quote candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(TQualified, std::integral_constant<int, 1> const)"
  "Template parser edge fixture lost the post-template qualifier candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(C8, u8'c')"
  "Template parser edge fixture lost the char8_t literal candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(C16, u'd')"
  "Template parser edge fixture lost the char16_t literal candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(C32, U'e')"
  "Template parser edge fixture lost the char32_t literal candidate")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/smoke/codegen_template_parser_edges.cpp"
  "template(CW, L'f')"
  "Template parser edge fixture lost the wchar_t literal candidate")

_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_manual_partial_includes/impl_cases.cppm"
  "#include \"gentest/mock_impl_codegen.h\""
  "Module manual partial include fixture lost the manual impl include")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_manual_partial_includes/registry_cases.cppm"
  "#include \"gentest/mock_registry_codegen.h\""
  "Module manual partial include fixture lost the manual registry include")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_partial_manual_codegen_includes/impl_cases.cppm"
  "#include \"gentest/mock_impl_codegen.h\""
  "Module partial manual codegen include fixture lost the impl include")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_partial_manual_codegen_includes/registry_cases.cppm"
  "#include \"gentest/mock_registry_codegen.h\""
  "Module partial manual codegen include fixture lost the registry include")

_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_header_unit_import_preamble/cases.cppm"
  "import <vector>;"
  "Header-unit import preamble fixture lost the header-unit import")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_partition_import_shorthand/cases.cppm"
  "export import /* partition */ :helpers;"
  "Module partition shorthand fixture lost the shorthand partition import")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_partition_import_shorthand/helpers.cppm"
  "module gentest.partition_import_cases:helpers;"
  "Module partition helper fixture lost the partition declaration")

_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_mock_imported_sibling/consumer.cppm"
  "import gentest.imported_sibling_mocks;"
  "Imported-sibling consumer fixture lost the sibling mock import")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_mock_imported_sibling/provider.ixx"
  "export /* provider */ module gentest.imported_sibling_provider;"
  "Imported-sibling provider fixture lost the provider module declaration")

_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/binary_expression.cppm"
  "#if 0b10u == 2u"
  "Binary-expression module false-match fixture lost the binary literal preprocessor form")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/digit_separated_expression.cppm"
  "#if 1'0u == 10u"
  "Digit-separated module false-match fixture lost the digit-separated literal form")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/has_include_include_dir.cppm"
  "__has_include(\"present_dir_header.hpp\")"
  "Directory __has_include false-match fixture lost the include-dir probe")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/has_include_macro.cppm"
  "#define HDR \"present_header.hpp\""
  "Macro __has_include false-match fixture lost the macro header indirection")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/has_include_true.cppm"
  "export module include.module;"
  "Positive __has_include fixture lost the exported module declaration")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/literal_false_match.cppm"
  "const char   *banner = \"export module wrong.literal;\";"
  "Literal false-match fixture lost the string-literal module decoy")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/literal_false_match.cppm"
  "export module real.module;"
  "Literal false-match fixture lost the real exported module declaration")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/octal_expression.cppm"
  "#if 010u == 8u"
  "Octal-expression module false-match fixture lost the octal literal preprocessor form")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/shift_expression.cppm"
  "#if (1u << 2) == 4u"
  "Shift-expression module false-match fixture lost the shift-expression form")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/conditional_false_match.ixx"
  "#if 0b10u == 2u && 010u == 8u && __has_include(HDR)"
  "Conditional false-match fixture lost the compound preprocessor expression")
_gentest_require_substring(
  "${SOURCE_DIR}/tests/cmake/module_name_literal_false_match/conditional_false_match.ixx"
  "export module real_conditional;"
  "Conditional false-match fixture lost the selected exported module declaration")
