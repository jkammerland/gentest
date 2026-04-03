if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckSyntaxSensitiveRegressionFixtures.cmake: SOURCE_DIR not set")
endif()

set(_multiline_mock_defs "${SOURCE_DIR}/tests/cmake/module_multiline_directives/mock_defs.cppm")
set(_install_header_mocks "${SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export/producer/header_mocks.hpp")
set(_install_module_support "${SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export/producer/service_module.cppm")

foreach(_required IN ITEMS
    "${_multiline_mock_defs}"
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
