if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckNonCmakeModuleWiring.cmake: SOURCE_DIR not set")
endif()

set(_meson_file "${SOURCE_DIR}/meson.build")
set(_xmake_file "${SOURCE_DIR}/xmake.lua")
set(_xmake_helper_file "${SOURCE_DIR}/xmake/gentest.lua")
set(_bazel_rules_file "${SOURCE_DIR}/build_defs/gentest.bzl")
set(_bazel_root_file "${SOURCE_DIR}/BUILD.bazel")

foreach(_required IN ITEMS
    "${_meson_file}"
    "${_xmake_file}"
    "${_xmake_helper_file}"
    "${_bazel_rules_file}"
    "${_bazel_root_file}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing non-CMake module wiring file: ${_required}")
  endif()
endforeach()

file(READ "${_meson_file}" _meson_content)
foreach(_expected IN ITEMS
    "gentest_consumer_textual_meson"
    "'--kind', 'textual'")
  string(FIND "${_meson_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "meson.build is missing expected repo-local textual wiring token: ${_expected}")
  endif()
endforeach()

file(READ "${_xmake_file}" _xmake_content)
foreach(_expected IN ITEMS
    "gentest_consumer_module_mocks_xmake"
    "gentest_consumer_module_xmake"
    "kind = \"modules\""
    "module_name = \"gentest.consumer_mocks\""
    "GENTEST_CONSUMER_USE_MODULES=1")
  string(FIND "${_xmake_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake.lua is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()

file(READ "${_xmake_helper_file}" _xmake_helper_content)
foreach(_expected IN ITEMS
    "function gentest_add_mocks(opts)"
    "function gentest_attach_codegen(opts)"
    "kind == \"modules\""
    "module_public_output_rel"
    "--mock-metadata")
  string(FIND "${_xmake_helper_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake/gentest.lua is missing repo-local module helper token: ${_expected}")
  endif()
endforeach()

file(READ "${_bazel_rules_file}" _bazel_rules_content)
foreach(_expected IN ITEMS
    "def gentest_add_mocks_modules("
    "def gentest_attach_codegen_modules("
    "compile_commands.json"
    "_mock_metadata"
    "same-package labels"
    "features = [\"cpp_modules\"]")
  string(FIND "${_bazel_rules_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "build_defs/gentest.bzl is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()

file(READ "${_bazel_root_file}" _bazel_root_content)
foreach(_expected IN ITEMS
    "gentest_add_mocks_modules("
    "gentest_attach_codegen_modules("
    "gentest_consumer_module_mocks"
    "gentest_consumer_module_bazel"
    "GENTEST_CONSUMER_USE_MODULES=1")
  string(FIND "${_bazel_root_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "BUILD.bazel is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()
