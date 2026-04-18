#[[
  Contract-only check for the checked-in non-CMake module wiring.
  Runtime proof of the supported backends lives in the direct Bazel/Xmake smoke
  tests and the Meson textual smoke test.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckNonCmakeModuleContract.cmake: SOURCE_DIR not set")
endif()

set(_meson_file "${SOURCE_DIR}/meson.build")
set(_xmake_file "${SOURCE_DIR}/xmake.lua")
set(_xmake_helper_file "${SOURCE_DIR}/xmake/gentest.lua")
set(_bazel_rules_file "${SOURCE_DIR}/build_defs/gentest.bzl")
set(_bazel_root_file "${SOURCE_DIR}/BUILD.bazel")
set(_bazel_module_file "${SOURCE_DIR}/MODULE.bazel")

foreach(_required IN ITEMS
    "${_meson_file}"
    "${_xmake_file}"
    "${_xmake_helper_file}"
    "${_bazel_rules_file}"
    "${_bazel_root_file}"
    "${_bazel_module_file}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing non-CMake module wiring file: ${_required}")
  endif()
endforeach()

file(READ "${_meson_file}" _meson_content)
foreach(_expected IN ITEMS
    "gentest_consumer_textual_meson"
    "Meson named-module support is"
    "wrapper_template = files('meson/tu_wrapper.cpp.in')"
    "'--depfile'"
    "'--artifact-manifest'"
    "'--artifact-owner-source'"
    "'--compile-context-id'"
    "depfile:")
  string(FIND "${_meson_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "meson.build is missing expected repo-local textual wiring token: ${_expected}")
  endif()
endforeach()
string(FIND "${_meson_content}" "gentest_buildsystem_codegen.py" _meson_helper_pos)
if(NOT _meson_helper_pos EQUAL -1)
  message(FATAL_ERROR "meson.build must not reference scripts/gentest_buildsystem_codegen.py anymore.")
endif()

file(READ "${_xmake_file}" _xmake_content)
foreach(_expected IN ITEMS
    "gentest_consumer_module_mocks_xmake"
    "gentest_consumer_module_xmake"
    "kind = \"modules\""
    "defs_modules = {\"gentest.consumer_service\", \"gentest.consumer_mock_defs\"}"
    "module_name = \"gentest.consumer_mocks\""
    "module_name = \"gentest.consumer_cases\""
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
    "requires `defs_modules` with one explicit module name per defs file"
    "module_public_output_rel"
    "module_registration_output_rel"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "\"--artifact-owner-source\""
    "\"--compile-context-id\""
    "registered_target_metadata()"
    "collect_mock_metadata_inputs")
  string(FIND "${_xmake_helper_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake/gentest.lua is missing repo-local module helper token: ${_expected}")
  endif()
endforeach()
string(FIND "${_xmake_helper_content}" "gentest_buildsystem_codegen.py" _xmake_helper_pos)
if(NOT _xmake_helper_pos EQUAL -1)
  message(FATAL_ERROR "xmake/gentest.lua must not reference scripts/gentest_buildsystem_codegen.py anymore.")
endif()

file(READ "${_bazel_rules_file}" _bazel_rules_content)
foreach(_expected IN ITEMS
    "GentestGeneratedInfo = provider("
    "def gentest_add_mocks_modules("
    "def gentest_attach_codegen_modules("
    "compile_commands.json"
    "ctx.actions.expand_template("
    "ctx.actions.write("
    "use_default_shell_env = True"
    "_gentest_module_registration_relpath"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "\"--artifact-owner-source\""
    "\"--compile-context-id\""
    "defs_modules"
    "output_group = \"artifact_manifests\""
    "features = [\"cpp_modules\"]")
  string(FIND "${_bazel_rules_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "build_defs/gentest.bzl is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()
string(FIND "${_bazel_rules_content}" "gentest_buildsystem_codegen.py" _bazel_helper_pos)
if(NOT _bazel_helper_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must not reference scripts/gentest_buildsystem_codegen.py anymore.")
endif()

file(READ "${_bazel_root_file}" _bazel_root_content)
foreach(_expected IN ITEMS
    "gentest_add_mocks_modules("
    "gentest_attach_codegen_modules("
    "gentest_consumer_module_mocks"
    "gentest_consumer_module_bazel"
    "defs_modules = ["
    "GENTEST_CONSUMER_USE_MODULES=1")
  string(FIND "${_bazel_root_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "BUILD.bazel is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()

file(READ "${_bazel_module_file}" _bazel_module_content)
foreach(_expected IN ITEMS
    "module(name = \"gentest\""
    "bazel_dep(name = \"rules_cc\""
    "bazel_dep(name = \"rules_shell\""
    "http_archive("
    "name = \"fmt\"")
  string(FIND "${_bazel_module_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "MODULE.bazel is missing repo-local module wiring token: ${_expected}")
  endif()
endforeach()
