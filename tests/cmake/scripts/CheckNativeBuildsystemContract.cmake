#[[
  Contract-only check for the native non-CMake backend surface.
  This script intentionally validates checked-in file wiring/tokens and is
  complemented by the runtime backend smoke tests.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckNativeBuildsystemContract.cmake: SOURCE_DIR not set")
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
    message(FATAL_ERROR "Missing native backend fixture: ${_required}")
  endif()
endforeach()

foreach(_obsolete IN ITEMS
    "${SOURCE_DIR}/meson/tu_wrapper.cpp.in"
    "${SOURCE_DIR}/meson/textual_mock_defs.cpp.in"
    "${SOURCE_DIR}/meson/anchor.cpp.in"
    "${SOURCE_DIR}/meson/textual_mock_public.hpp.in")
  if(EXISTS "${_obsolete}")
    message(FATAL_ERROR "Obsolete Meson template should be codegen-owned now: ${_obsolete}")
  endif()
endforeach()

file(READ "${_meson_file}" _meson_content)
string(FIND "${_meson_content}" "gentest_buildsystem_codegen.py" _meson_helper_pos)
if(NOT _meson_helper_pos EQUAL -1)
  message(FATAL_ERROR "meson.build must not route textual codegen through scripts/gentest_buildsystem_codegen.py anymore.")
endif()
foreach(_expected IN ITEMS
    "Meson named-module support is"
    "'gentest_consumer_textual_meson'"
    "'gen_consumer_textual_mocks'"
    "'--tu-out-dir'"
    "'--textual-wrapper-output'"
    "'--mock-public-header'"
    "'--depfile'"
    "'--artifact-manifest'"
    "depfile:")
  string(FIND "${_meson_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "meson.build is missing expected native-textual token: ${_expected}")
  endif()
endforeach()

file(READ "${_xmake_file}" _xmake_content)
file(READ "${_xmake_helper_file}" _xmake_helper_content)
foreach(_content_name IN ITEMS "_xmake_content" "_xmake_helper_content")
  string(FIND "${${_content_name}}" "gentest_buildsystem_codegen.py" _helper_pos)
  if(NOT _helper_pos EQUAL -1)
    message(FATAL_ERROR "Xmake integration must not reference scripts/gentest_buildsystem_codegen.py anymore (${_content_name}).")
  endif()
endforeach()
foreach(_expected IN ITEMS
    "function gentest_add_mocks(opts)"
    "function gentest_attach_codegen(opts)"
    "run_command(batchcmds, codegen, args)"
    "registered_target_metadata()"
    "collect_mock_metadata_inputs"
    "module_public_output_rel"
    "module_registration_output_rel"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "requires `defs_modules` with one explicit module name per defs file"
    "kind == \"modules\"")
  string(FIND "${_xmake_helper_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake/gentest.lua is missing expected native Xmake token: ${_expected}")
  endif()
endforeach()

file(READ "${_bazel_rules_file}" _bazel_rules_content)
string(FIND "${_bazel_rules_content}" "gentest_buildsystem_codegen.py" _bazel_helper_pos)
if(NOT _bazel_helper_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must not route codegen through scripts/gentest_buildsystem_codegen.py anymore.")
endif()
foreach(_expected IN ITEMS
    "GentestGeneratedInfo = provider("
    "_gentest_textual_codegen = rule("
    "_gentest_module_mocks_codegen = rule("
    "_gentest_module_suite_codegen = rule("
    "ctx.actions.expand_template("
    "ctx.actions.write("
    "ctx.actions.run("
    "use_default_shell_env = False"
    "_gentest_run_codegen("
    "_GENTEST_CODEGEN_TOOLCHAIN_TYPE"
    "execution_requirements = execution_requirements"
    "\"no-remote\": \"1\""
    "source_hdrs"
    "source_deps"
    "codegen_support.headers"
    "codegen_support.defines"
    "codegen_support.framework_include_dirs"
    "defs_modules"
    "\"--mock-aggregate-module-output\""
    "\"--mock-aggregate-module-name\""
    "public_module = ctx.actions.declare_file"
    "_gentest_module_registration_relpath"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "_gentest_codegen_target("
    "output_group = \"module_interfaces\"")
  string(FIND "${_bazel_rules_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "build_defs/gentest.bzl is missing expected native Bazel token: ${_expected}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/bazel/local_exec_tools.bzl" _bazel_local_tools_content)
foreach(_expected IN ITEMS
    "os_name = repository_ctx.os.name.lower()"
    "os_name.find(\"windows\")"
    "target_compatible_with = [\":unavailable\"]")
  string(FIND "${_bazel_local_tools_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "bazel/local_exec_tools.bzl is missing Windows fallback safety token: ${_expected}")
  endif()
endforeach()

foreach(_bazel_consumer_script IN ITEMS CheckBazelTextualConsumer.cmake CheckBazelModuleConsumer.cmake)
  file(READ "${SOURCE_DIR}/tests/cmake/scripts/${_bazel_consumer_script}" _bazel_consumer_script_content)
  foreach(_expected IN ITEMS
      "if(WIN32)"
      "automatic local exec-tool fallback is disabled"
      "must register a packaged Gentest/Clang toolchain")
    string(FIND "${_bazel_consumer_script_content}" "${_expected}" _expected_pos)
    if(_expected_pos EQUAL -1)
      message(FATAL_ERROR "${_bazel_consumer_script} is missing Windows toolchain skip token: ${_expected}")
    endif()
  endforeach()
endforeach()

file(READ "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelCodegenActionCache.cmake" _bazel_cache_script_content)
foreach(_expected IN ITEMS
    "find_program(_found_clang_c"
    "set(_clang_c \"\${_found_clang_c}\")")
  string(FIND "${_bazel_cache_script_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "CheckBazelCodegenActionCache.cmake must search for a versioned clang through a fresh cache variable: ${_expected}")
  endif()
endforeach()

file(READ "${_bazel_root_file}" _bazel_root_content)
foreach(_expected IN ITEMS
    "gentest_add_mocks_modules("
    "defs_modules = ["
    "gentest_attach_codegen_modules("
    "source_hdrs = ['tests/consumer/bazel_private_case_value.hpp']"
    "gentest_consumer_codegen_headers"
    "gentest_consumer_module_mocks"
    "gentest_consumer_module_bazel")
  string(FIND "${_bazel_root_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "BUILD.bazel is missing expected native Bazel wiring token: ${_expected}")
  endif()
endforeach()
