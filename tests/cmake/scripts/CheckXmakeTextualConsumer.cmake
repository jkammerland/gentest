if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

function(_gentest_resolve_xmake_test_tool out_var raw_value label)
  if("${raw_value}" STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  if(IS_ABSOLUTE "${raw_value}")
    if(NOT EXISTS "${raw_value}")
      message(FATAL_ERROR
        "CheckXmakeTextualConsumer.cmake: ${label} does not exist: ${raw_value}")
    endif()
    set(${out_var} "${raw_value}" PARENT_SCOPE)
    return()
  endif()

  find_program(_resolved_tool NAMES "${raw_value}")
  if(NOT _resolved_tool)
    message(FATAL_ERROR
      "CheckXmakeTextualConsumer.cmake: failed to resolve ${label} from '${raw_value}'.")
  endif()
  set(${out_var} "${_resolved_tool}" PARENT_SCOPE)
endfunction()

function(_gentest_prepare_windows_xmake_workspace out_var source_dir workspace_root)
  set(_project_dir "${workspace_root}/xw")
  file(REMOVE_RECURSE "${_project_dir}")
  file(MAKE_DIRECTORY "${_project_dir}")
  foreach(_entry IN ITEMS
      cmake CMakeLists.txt CMakePresets.json docs include src tests third_party tools vcpkg.json xmake xmake.lua)
    if(EXISTS "${source_dir}/${_entry}")
      file(COPY "${source_dir}/${_entry}" DESTINATION "${_project_dir}")
    endif()
  endforeach()
  set(${out_var} "${_project_dir}" PARENT_SCOPE)
endfunction()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

set(_gentest_xmake_root "${CMAKE_CURRENT_BINARY_DIR}")
if(DEFINED BUILD_ROOT AND NOT "${BUILD_ROOT}" STREQUAL "")
  set(_gentest_xmake_root "${BUILD_ROOT}")
endif()

set(_dep_cache_unit_dir "${_gentest_xmake_root}/xmake_dep_cache_unit")
file(REMOVE_RECURSE "${_dep_cache_unit_dir}")
execute_process(
  COMMAND "${_xmake}" lua "${SOURCE_DIR}/tests/xmake/check_codegen_dep_cache.lua"
          parser "${SOURCE_DIR}/xmake/scripts/update_codegen_dep_cache.lua" "${_dep_cache_unit_dir}/parser"
  RESULT_VARIABLE _dep_parser_rc
  OUTPUT_VARIABLE _dep_parser_out
  ERROR_VARIABLE _dep_parser_err)
if(NOT _dep_parser_rc EQUAL 0)
  message(FATAL_ERROR
    "The Xmake codegen depfile parser did not preserve Make escapes/literal backslashes.\n"
    "stdout:\n${_dep_parser_out}\nstderr:\n${_dep_parser_err}")
endif()

if(NOT WIN32)
  find_program(_sh NAMES sh)
  if(_sh)
    set(_race_dir "${_dep_cache_unit_dir}/race")
    set(_race_cache "${_race_dir}/snapshot")
    file(MAKE_DIRECTORY "${_race_dir}")
    file(WRITE "${_race_dir}/dependency.hpp" "#pragma once\n")
    file(WRITE "${_race_dir}/generated.d" "output: dependency.hpp\n")
    gentest_fixture_join_posix_shell_command(_race_writer_a
      "${_xmake}" lua "${SOURCE_DIR}/xmake/scripts/update_codegen_dep_cache.lua"
      "${_race_cache}" "${_race_dir}/generated.d" race-identity "${_race_dir}")
    gentest_fixture_join_posix_shell_command(_race_writer_b
      "${_xmake}" lua "${SOURCE_DIR}/xmake/scripts/update_codegen_dep_cache.lua"
      "${_race_cache}" "${_race_dir}/generated.d" race-identity "${_race_dir}")
    execute_process(
      COMMAND "${_sh}" -c
        "${_race_writer_a} & p1=$!; ${_race_writer_b} & p2=$!; wait \"$p1\"; a=$?; wait \"$p2\"; b=$?; test \"$a\" -eq 0 && test \"$b\" -eq 0"
      RESULT_VARIABLE _dep_race_rc
      OUTPUT_VARIABLE _dep_race_out
      ERROR_VARIABLE _dep_race_err)
    if(NOT _dep_race_rc EQUAL 0)
      message(FATAL_ERROR
        "Concurrent Xmake dependency-cache writers failed.\n"
        "stdout:\n${_dep_race_out}\nstderr:\n${_dep_race_err}")
    endif()
    execute_process(
      COMMAND "${_xmake}" lua "${SOURCE_DIR}/tests/xmake/check_codegen_dep_cache.lua"
              validate "${_race_cache}" race-identity
      RESULT_VARIABLE _dep_race_validate_rc
      OUTPUT_VARIABLE _dep_race_validate_out
      ERROR_VARIABLE _dep_race_validate_err)
    if(NOT _dep_race_validate_rc EQUAL 0)
      message(FATAL_ERROR
        "Concurrent Xmake dependency-cache publication was not atomic.\n"
        "stdout:\n${_dep_race_validate_out}\nstderr:\n${_dep_race_validate_err}")
    endif()

    set(_owner_dir "${_dep_cache_unit_dir}/owner-race")
    set(_owner_cache "${_owner_dir}/snapshot")
    set(_owner_output "${_owner_dir}/generated.txt")
    set(_owner_depfile "${_owner_dir}/generated.d")
    file(MAKE_DIRECTORY "${_owner_dir}")
    gentest_fixture_join_posix_shell_command(_owner_writer_a
      "${_xmake}" lua "${SOURCE_DIR}/xmake/scripts/run_codegen_with_dep_cache.lua"
      "${_owner_cache}" "${_owner_depfile}" identity-a "${_owner_dir}" 1 "${_xmake}"
      "${_owner_output}" lua "${SOURCE_DIR}/tests/xmake/fake_codegen_cache_writer.lua"
      "${_owner_output}" "${_owner_depfile}" identity-a 50)
    gentest_fixture_join_posix_shell_command(_owner_writer_b
      "${_xmake}" lua "${SOURCE_DIR}/xmake/scripts/run_codegen_with_dep_cache.lua"
      "${_owner_cache}" "${_owner_depfile}" identity-b "${_owner_dir}" 1 "${_xmake}"
      "${_owner_output}" lua "${SOURCE_DIR}/tests/xmake/fake_codegen_cache_writer.lua"
      "${_owner_output}" "${_owner_depfile}" identity-b 0)
    execute_process(
      COMMAND "${_sh}" -c
        "${_owner_writer_a} & p1=$!; ${_owner_writer_b} & p2=$!; wait \"$p1\"; a=$?; wait \"$p2\"; b=$?; test \"$a\" -eq 0 && test \"$b\" -eq 0"
      RESULT_VARIABLE _owner_race_rc
      OUTPUT_VARIABLE _owner_race_out
      ERROR_VARIABLE _owner_race_err)
    if(NOT _owner_race_rc EQUAL 0)
      message(FATAL_ERROR
        "Concurrent divergent Xmake codegen owners failed.\n"
        "stdout:\n${_owner_race_out}\nstderr:\n${_owner_race_err}")
    endif()
    execute_process(
      COMMAND "${_xmake}" lua "${SOURCE_DIR}/tests/xmake/check_codegen_dep_cache.lua"
              validate-output "${_owner_cache}" "${_owner_output}"
      RESULT_VARIABLE _owner_validate_rc
      OUTPUT_VARIABLE _owner_validate_out
      ERROR_VARIABLE _owner_validate_err)
    if(NOT _owner_validate_rc EQUAL 0)
      message(FATAL_ERROR
        "Divergent Xmake codegen snapshot does not own its generated output.\n"
        "stdout:\n${_owner_validate_out}\nstderr:\n${_owner_validate_err}")
    endif()
  endif()
endif()

# The incremental cases edit fixture inputs and Xmake configuration. Keep
# every platform in a private copy so this script never mutates the shared
# source tree when CTest runs alongside other build-system checks.
_gentest_prepare_windows_xmake_workspace(_project_dir "${SOURCE_DIR}" "${_gentest_xmake_root}")

# `target:name()` strips Xmake namespaces while public helper callers pass the
# full target name. Keep a small namespaced target in this private workspace to
# exercise the rule's canonical lookup without changing the repository build.
file(APPEND "${_project_dir}/xmake.lua"
"\n\ntarget(\"xmake_probe::namespaced_textual\")
    set_kind(\"static\")
    gentest_apply_windows_llvm_toolchain()
    gentest_attach_codegen({
        name = \"xmake_probe::namespaced_textual\",
        kind = \"textual\",
        source = \"tests/consumer/cases.cpp\",
        output_dir = path.join(current_gen_root(), \"namespaced_textual\"),
        deps = {\"gentest_runtime\", \"gentest_consumer_textual_mocks_xmake\"},
    })
")

set(_gentest_clang_search_paths "")
foreach(_compiler_path IN ITEMS "${CXX_COMPILER}" "${C_COMPILER}")
  if(_compiler_path)
    get_filename_component(_compiler_realpath "${_compiler_path}" REALPATH)
    if(_compiler_realpath)
      get_filename_component(_compiler_dir "${_compiler_realpath}" DIRECTORY)
    else()
      get_filename_component(_compiler_dir "${_compiler_path}" DIRECTORY)
    endif()
    list(APPEND _gentest_clang_search_paths "${_compiler_dir}")
  endif()
endforeach()
foreach(_cmake_dir IN ITEMS "${Clang_DIR}" "${LLVM_DIR}")
  if(_cmake_dir)
    get_filename_component(_llvm_prefix "${_cmake_dir}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    list(APPEND _gentest_clang_search_paths "${_llvm_prefix}/bin")
  endif()
endforeach()
if(APPLE)
  list(APPEND _gentest_clang_search_paths
    /opt/homebrew/opt/llvm@23/bin
    /opt/homebrew/opt/llvm@22/bin
    /opt/homebrew/opt/llvm@21/bin
    /opt/homebrew/opt/llvm@20/bin
    /usr/local/opt/llvm/bin)
endif()
list(APPEND _gentest_clang_search_paths
  /usr/bin
  /bin
  /usr/lib64/llvm23/bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-23/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin)
list(REMOVE_DUPLICATES _gentest_clang_search_paths)

find_program(_clang_cxx NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

set(_target_cxx "${_clang_cxx}")
set(_target_cc "${_clang_cc}")
set(_configured_target_cc "$ENV{GENTEST_XMAKE_TEST_TARGET_CC}")
set(_configured_target_cxx "$ENV{GENTEST_XMAKE_TEST_TARGET_CXX}")
set(_has_configured_target_cc FALSE)
set(_has_configured_target_cxx FALSE)
if(NOT "${_configured_target_cc}" STREQUAL "")
  set(_has_configured_target_cc TRUE)
endif()
if(NOT "${_configured_target_cxx}" STREQUAL "")
  set(_has_configured_target_cxx TRUE)
endif()
if(_has_configured_target_cc AND NOT _has_configured_target_cxx)
  message(FATAL_ERROR
    "CheckXmakeTextualConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
if(_has_configured_target_cxx AND NOT _has_configured_target_cc)
  message(FATAL_ERROR
    "CheckXmakeTextualConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
if(NOT "${_configured_target_cc}" STREQUAL "")
  _gentest_resolve_xmake_test_tool(_target_cc "${_configured_target_cc}" "GENTEST_XMAKE_TEST_TARGET_CC")
  _gentest_resolve_xmake_test_tool(_target_cxx "${_configured_target_cxx}" "GENTEST_XMAKE_TEST_TARGET_CXX")
endif()

set(_out_dir "${_project_dir}/build")
set(_xmake_global_dir "${_gentest_xmake_root}/xg")
if(WIN32)
  set(_out_dir "${_project_dir}/b")
endif()
file(REMOVE_RECURSE "${_out_dir}")
file(REMOVE_RECURSE "${_xmake_global_dir}")
file(MAKE_DIRECTORY "${_out_dir}/tmp")

set(_xmake_env
  "--unset=GENTEST_CODEGEN_RESOURCE_DIR"
  "--unset=GENTEST_CODEGEN_SCAN_DEPS_MODE"
  "--unset=GENTEST_CODEGEN_PARSE_CACHE"
  "--unset=GENTEST_CODEGEN_PARSE_CACHE_DIR"
  "--unset=GENTEST_STRICT_FIXTURE"
  "--unset=GENTEST_NO_INCLUDE_SOURCES"
  "GENTEST_CODEGEN=${_codegen}"
  "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
  "GENTEST_XMAKE_SKIP_MODULE_TARGETS=1"
  "CC=${_target_cc}"
  "CXX=${_target_cxx}"
  "TMPDIR=${_out_dir}/tmp"
  "XMAKE_GLOBALDIR=${_xmake_global_dir}")

set(_xmake_config_args
  f -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  "--cc=${_target_cc}"
  "--cxx=${_target_cxx}")
set(_xmake_reconfigure_args
  f -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -o "${_out_dir}" -m debug -y
  "--cc=${_target_cc}"
  "--cxx=${_target_cxx}")
set(_xmake_build_args
  build -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -y -vD)
if(WIN32)
  set(_xmake_config_args
    f -P . -F xmake.lua -o "${_out_dir}" -m debug -c -y
    "--cc=${_target_cc}"
    "--cxx=${_target_cxx}")
  set(_xmake_reconfigure_args
    f -P . -F xmake.lua -o "${_out_dir}" -m debug -y
    "--cc=${_target_cc}"
    "--cxx=${_target_cxx}")
  set(_xmake_build_args
    build -P . -F xmake.lua -y -vD)
  list(APPEND _xmake_config_args "--toolchain=llvm")
  list(APPEND _xmake_reconfigure_args "--toolchain=llvm")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_config_args}
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE _cfg_err)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake configure failed for the textual consumer smoke check.\n"
    "stdout:\n${_cfg_out}\n"
    "stderr:\n${_cfg_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _mock_build_rc
  OUTPUT_VARIABLE _mock_build_out
  ERROR_VARIABLE _mock_build_err)
if(NOT _mock_build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_textual_mocks_xmake.\n"
    "stdout:\n${_mock_build_out}\n"
    "stderr:\n${_mock_build_err}")
endif()
set(_mock_initial_log "${_mock_build_out}\n${_mock_build_err}")
foreach(_expected IN ITEMS
    "--host-clang"
    "${_clang_cxx}"
    "-DGENTEST_XMAKE_TEXTUAL_MOCKS_DEFINE=1"
    "-DGENTEST_XMAKE_TEXTUAL_MOCKS_CODEGEN=1")
  string(FIND "${_mock_initial_log}" "${_expected}" _mock_initial_pos)
  if(_mock_initial_pos EQUAL -1)
    message(FATAL_ERROR
      "The initial textual mock codegen command did not include '${_expected}'.\n"
      "stdout/stderr:\n${_mock_initial_log}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          xmake_probe::namespaced_textual
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _namespaced_rc
  OUTPUT_VARIABLE _namespaced_out
  ERROR_VARIABLE _namespaced_err)
if(NOT _namespaced_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for the namespaced textual helper target.\n"
    "stdout:\n${_namespaced_out}\n"
    "stderr:\n${_namespaced_err}")
endif()
set(_namespaced_log "${_namespaced_out}\n${_namespaced_err}")
string(FIND "${_namespaced_log}" "--source-root" _namespaced_codegen_pos)
if(_namespaced_codegen_pos EQUAL -1)
  message(FATAL_ERROR "The namespaced textual helper target did not run gentest_codegen.\n${_namespaced_log}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_textual_xmake.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()
set(_suite_initial_log "${_build_out}\n${_build_err}")
foreach(_expected IN ITEMS
    "--host-clang"
    "${_clang_cxx}"
    "-DGENTEST_XMAKE_TEXTUAL_CONSUMER_DEFINE=1"
    "-DGENTEST_XMAKE_TEXTUAL_CONSUMER_CODEGEN=1")
  string(FIND "${_suite_initial_log}" "${_expected}" _suite_initial_pos)
  if(_suite_initial_pos EQUAL -1)
    message(FATAL_ERROR
      "The initial textual suite codegen command did not include '${_expected}'.\n"
      "stdout/stderr:\n${_suite_initial_log}")
  endif()
endforeach()
string(FIND "${_mock_initial_log}\n${_suite_initial_log}" "--parse-cache-dir" _default_parse_cache_flag_pos)
if(NOT _default_parse_cache_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "The default Xmake textual build unexpectedly enabled the parse cache.\n"
    "mock stdout/stderr:\n${_mock_initial_log}\n"
    "suite stdout/stderr:\n${_suite_initial_log}")
endif()

# A fresh build followed immediately by the same build must not schedule the
# generator again. This checks the sidecar-backed dynamic dependency closure,
# not merely preservation of a generated output timestamp.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _mock_noop_rc
  OUTPUT_VARIABLE _mock_noop_out
  ERROR_VARIABLE _mock_noop_err)
if(NOT _mock_noop_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake no-op build failed for gentest_consumer_textual_mocks_xmake.\n"
    "stdout:\n${_mock_noop_out}\n"
    "stderr:\n${_mock_noop_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _noop_rc
  OUTPUT_VARIABLE _noop_out
  ERROR_VARIABLE _noop_err)
if(NOT _noop_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake no-op build failed for gentest_consumer_textual_xmake.\n"
    "stdout:\n${_noop_out}\n"
    "stderr:\n${_noop_err}")
endif()

set(_noop_log "${_mock_noop_out}\n${_mock_noop_err}\n${_noop_out}\n${_noop_err}")
string(FIND "${_noop_log}" "--source-root" _noop_codegen_pos)
if(NOT _noop_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "An unchanged Xmake textual build reran gentest_codegen.\n"
    "stdout/stderr:\n${_noop_log}")
endif()
foreach(_incremental_marker IN ITEMS "compiling." "linking.")
  string(FIND "${_noop_log}" "${_incremental_marker}" _incremental_marker_pos)
  if(NOT _incremental_marker_pos EQUAL -1)
    message(FATAL_ERROR
      "An unchanged Xmake textual build unexpectedly scheduled '${_incremental_marker}'.\n"
      "stdout/stderr:\n${_noop_log}")
  endif()
endforeach()

# Output-affecting ambient codegen settings are part of the sidecar identity.
# Changing either one must schedule generation once; repeating it must be a
# true no-op. Keep each setting in the base environment after its probe so
# later incremental checks do not accidentally switch the identity back.
set(_scan_mode_env "GENTEST_CODEGEN_SCAN_DEPS_MODE=OFF")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_scan_mode_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _scan_mode_rc
  OUTPUT_VARIABLE _scan_mode_out
  ERROR_VARIABLE _scan_mode_err)
if(NOT _scan_mode_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after changing GENTEST_CODEGEN_SCAN_DEPS_MODE.\n${_scan_mode_out}\n${_scan_mode_err}")
endif()
set(_scan_mode_log "${_scan_mode_out}\n${_scan_mode_err}")
string(FIND "${_scan_mode_log}" "--source-root" _scan_mode_codegen_pos)
if(_scan_mode_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Changing GENTEST_CODEGEN_SCAN_DEPS_MODE did not rerun gentest_codegen.\n${_scan_mode_log}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_scan_mode_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _scan_mode_noop_rc
  OUTPUT_VARIABLE _scan_mode_noop_out
  ERROR_VARIABLE _scan_mode_noop_err)
if(NOT _scan_mode_noop_rc EQUAL 0)
  message(FATAL_ERROR "xmake no-op failed with stable GENTEST_CODEGEN_SCAN_DEPS_MODE.\n${_scan_mode_noop_out}\n${_scan_mode_noop_err}")
endif()
set(_scan_mode_noop_log "${_scan_mode_noop_out}\n${_scan_mode_noop_err}")
string(FIND "${_scan_mode_noop_log}" "--source-root" _scan_mode_noop_codegen_pos)
if(NOT _scan_mode_noop_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Stable GENTEST_CODEGEN_SCAN_DEPS_MODE did not produce a no-op build.\n${_scan_mode_noop_log}")
endif()
list(APPEND _xmake_env "${_scan_mode_env}")

execute_process(
  COMMAND "${_clang_cxx}" -print-resource-dir
  RESULT_VARIABLE _resource_dir_rc
  OUTPUT_VARIABLE _resource_dir
  ERROR_VARIABLE _resource_dir_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _resource_dir_rc EQUAL 0 OR NOT IS_DIRECTORY "${_resource_dir}")
  message(FATAL_ERROR "Could not resolve Clang's resource directory for the Xmake identity regression.\n${_resource_dir_err}")
endif()
set(_resource_dir_env "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_resource_dir_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _resource_dir_build_rc
  OUTPUT_VARIABLE _resource_dir_build_out
  ERROR_VARIABLE _resource_dir_build_err)
if(NOT _resource_dir_build_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after changing GENTEST_CODEGEN_RESOURCE_DIR.\n${_resource_dir_build_out}\n${_resource_dir_build_err}")
endif()
set(_resource_dir_build_log "${_resource_dir_build_out}\n${_resource_dir_build_err}")
string(FIND "${_resource_dir_build_log}" "--source-root" _resource_dir_codegen_pos)
if(_resource_dir_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Changing GENTEST_CODEGEN_RESOURCE_DIR did not rerun gentest_codegen.\n${_resource_dir_build_log}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_resource_dir_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _resource_dir_noop_rc
  OUTPUT_VARIABLE _resource_dir_noop_out
  ERROR_VARIABLE _resource_dir_noop_err)
if(NOT _resource_dir_noop_rc EQUAL 0)
  message(FATAL_ERROR "xmake no-op failed with stable GENTEST_CODEGEN_RESOURCE_DIR.\n${_resource_dir_noop_out}\n${_resource_dir_noop_err}")
endif()
set(_resource_dir_noop_log "${_resource_dir_noop_out}\n${_resource_dir_noop_err}")
string(FIND "${_resource_dir_noop_log}" "--source-root" _resource_dir_noop_codegen_pos)
if(NOT _resource_dir_noop_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Stable GENTEST_CODEGEN_RESOURCE_DIR did not produce a no-op build.\n${_resource_dir_noop_log}")
endif()
list(APPEND _xmake_env "${_resource_dir_env}")

set(_strict_fixture_env "GENTEST_STRICT_FIXTURE=1")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_strict_fixture_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _strict_fixture_rc
  OUTPUT_VARIABLE _strict_fixture_out
  ERROR_VARIABLE _strict_fixture_err)
if(NOT _strict_fixture_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed after changing GENTEST_STRICT_FIXTURE.\n${_strict_fixture_out}\n${_strict_fixture_err}")
endif()
set(_strict_fixture_log "${_strict_fixture_out}\n${_strict_fixture_err}")
string(FIND "${_strict_fixture_log}" "--source-root" _strict_fixture_codegen_pos)
if(_strict_fixture_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Changing GENTEST_STRICT_FIXTURE did not rerun gentest_codegen.\n${_strict_fixture_log}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "${_strict_fixture_env}"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _strict_fixture_noop_rc
  OUTPUT_VARIABLE _strict_fixture_noop_out
  ERROR_VARIABLE _strict_fixture_noop_err)
if(NOT _strict_fixture_noop_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake no-op failed with stable GENTEST_STRICT_FIXTURE.\n${_strict_fixture_noop_out}\n${_strict_fixture_noop_err}")
endif()
set(_strict_fixture_noop_log "${_strict_fixture_noop_out}\n${_strict_fixture_noop_err}")
string(FIND "${_strict_fixture_noop_log}" "--source-root" _strict_fixture_noop_codegen_pos)
if(NOT _strict_fixture_noop_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Stable GENTEST_STRICT_FIXTURE did not produce a no-op build.\n${_strict_fixture_noop_log}")
endif()
list(APPEND _xmake_env "${_strict_fixture_env}")

# An unrelated file must leave the discovered closure untouched.
file(WRITE "${_project_dir}/tests/consumer/xmake_incremental_unrelated.hpp" "#pragma once\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _unrelated_rc
  OUTPUT_VARIABLE _unrelated_out
  ERROR_VARIABLE _unrelated_err)
if(NOT _unrelated_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after an unrelated fixture file edit.\n${_unrelated_out}\n${_unrelated_err}")
endif()
set(_unrelated_log "${_unrelated_out}\n${_unrelated_err}")
string(FIND "${_unrelated_log}" "--source-root" _unrelated_codegen_pos)
if(NOT _unrelated_codegen_pos EQUAL -1)
  message(FATAL_ERROR "An unrelated fixture file edit reran textual codegen.\n${_unrelated_log}")
endif()

# Source and both direct/private plus shared header edits are tracked. These
# are comment-only edits in the private workspace, so generated consumers stay
# semantically valid while the command scheduling is exercised.
file(APPEND "${_project_dir}/tests/consumer/cases.cpp" "\n// xmake source dependency regression\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _source_edit_rc
  OUTPUT_VARIABLE _source_edit_out
  ERROR_VARIABLE _source_edit_err)
if(NOT _source_edit_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after the textual source edit.\n${_source_edit_out}\n${_source_edit_err}")
endif()
set(_source_edit_log "${_source_edit_out}\n${_source_edit_err}")
string(FIND "${_source_edit_log}" "--source-root" _source_edit_codegen_pos)
if(_source_edit_codegen_pos EQUAL -1)
  message(FATAL_ERROR "A textual source edit did not rerun gentest_codegen.\n${_source_edit_log}")
endif()

file(APPEND "${_project_dir}/tests/consumer/header_mock_defs.hpp" "\n// xmake private header dependency regression\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _private_header_rc
  OUTPUT_VARIABLE _private_header_out
  ERROR_VARIABLE _private_header_err)
if(NOT _private_header_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after the private mock header edit.\n${_private_header_out}\n${_private_header_err}")
endif()
set(_private_header_log "${_private_header_out}\n${_private_header_err}")
string(FIND "${_private_header_log}" "--source-root" _private_header_codegen_pos)
if(_private_header_codegen_pos EQUAL -1)
  message(FATAL_ERROR "A private mock header edit did not rerun gentest_codegen.\n${_private_header_log}")
endif()

file(APPEND "${_project_dir}/tests/consumer/additional_header_mock_defs.hpp"
  "\n// xmake additional mock defs dependency regression\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _additional_defs_rc
  OUTPUT_VARIABLE _additional_defs_out
  ERROR_VARIABLE _additional_defs_err)
if(NOT _additional_defs_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed after the additional mock defs edit.\n${_additional_defs_out}\n${_additional_defs_err}")
endif()
set(_additional_defs_log "${_additional_defs_out}\n${_additional_defs_err}")
string(FIND "${_additional_defs_log}" "--source-root" _additional_defs_codegen_pos)
if(_additional_defs_codegen_pos EQUAL -1)
  message(FATAL_ERROR "An additional mock defs edit did not rerun gentest_codegen.\n${_additional_defs_log}")
endif()

file(APPEND "${_project_dir}/tests/consumer/service.hpp" "\n// xmake shared header dependency regression\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _shared_header_rc
  OUTPUT_VARIABLE _shared_header_out
  ERROR_VARIABLE _shared_header_err)
if(NOT _shared_header_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after the shared mock header edit.\n${_shared_header_out}\n${_shared_header_err}")
endif()
set(_shared_header_log "${_shared_header_out}\n${_shared_header_err}")
string(FIND "${_shared_header_log}" "--source-root" _shared_header_codegen_pos)
if(_shared_header_codegen_pos EQUAL -1)
  message(FATAL_ERROR "A shared mock header edit did not rerun gentest_codegen.\n${_shared_header_log}")
endif()

# Codegen's effective flags are part of the identity, even when the source
# graph itself is unchanged. Reconfigure after modifying only that flag.
file(READ "${_project_dir}/xmake.lua" _xmake_contents)
string(REPLACE "GENTEST_XMAKE_TEXTUAL_CONSUMER_CODEGEN=1" "GENTEST_XMAKE_TEXTUAL_CONSUMER_CODEGEN=2"
  _xmake_contents "${_xmake_contents}")
file(WRITE "${_project_dir}/xmake.lua" "${_xmake_contents}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_reconfigure_args}
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _flag_config_rc
  OUTPUT_VARIABLE _flag_config_out
  ERROR_VARIABLE _flag_config_err)
if(NOT _flag_config_rc EQUAL 0)
  message(FATAL_ERROR "xmake reconfigure failed after the codegen flag edit.\n${_flag_config_out}\n${_flag_config_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _flag_edit_rc
  OUTPUT_VARIABLE _flag_edit_out
  ERROR_VARIABLE _flag_edit_err)
if(NOT _flag_edit_rc EQUAL 0)
  message(FATAL_ERROR "xmake build failed after the codegen flag edit.\n${_flag_edit_out}\n${_flag_edit_err}")
endif()
set(_flag_edit_log "${_flag_edit_out}\n${_flag_edit_err}")
string(FIND "${_flag_edit_log}" "--source-root" _flag_edit_codegen_pos)
if(_flag_edit_codegen_pos EQUAL -1)
  message(FATAL_ERROR "A relevant codegen flag edit did not rerun gentest_codegen.\n${_flag_edit_log}")
endif()

# Parse caching is opt-in. The initial no-flag assertion is made immediately
# after initial mock/suite codegen above; this section only checks enabled
# directory forwarding after the explicit reconfiguration.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_reconfigure_args}
          --gentest_codegen_parse_cache=y
          --gentest_codegen_parse_cache_dir=cache/gentest
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _parse_config_rc
  OUTPUT_VARIABLE _parse_config_out
  ERROR_VARIABLE _parse_config_err)
if(NOT _parse_config_rc EQUAL 0)
  message(FATAL_ERROR "xmake parse-cache configuration failed.\n${_parse_config_out}\n${_parse_config_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _parse_build_rc
  OUTPUT_VARIABLE _parse_build_out
  ERROR_VARIABLE _parse_build_err)
if(NOT _parse_build_rc EQUAL 0)
  message(FATAL_ERROR "xmake parse-cache build failed.\n${_parse_build_out}\n${_parse_build_err}")
endif()
get_filename_component(_parse_cache_dir "${_out_dir}/cache/gentest" ABSOLUTE)
set(_parse_build_log "${_parse_build_out}\n${_parse_build_err}")
string(FIND "${_parse_build_log}" "--parse-cache-dir" _parse_cache_flag_pos)
string(FIND "${_parse_build_log}" "${_parse_cache_dir}" _parse_cache_dir_pos)
if(_parse_cache_flag_pos EQUAL -1 OR _parse_cache_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "An enabled relative Xmake parse cache was not forwarded as a build-owned path.\n"
    "Expected: ${_parse_cache_dir}\n${_parse_build_log}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_reconfigure_args}
          --gentest_codegen_parse_cache=y
          --gentest_codegen_parse_cache_dir=
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _default_parse_config_rc
  OUTPUT_VARIABLE _default_parse_config_out
  ERROR_VARIABLE _default_parse_config_err)
if(NOT _default_parse_config_rc EQUAL 0)
  message(FATAL_ERROR "xmake default parse-cache configuration failed.\n${_default_parse_config_out}\n${_default_parse_config_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _default_parse_build_rc
  OUTPUT_VARIABLE _default_parse_build_out
  ERROR_VARIABLE _default_parse_build_err)
if(NOT _default_parse_build_rc EQUAL 0)
  message(FATAL_ERROR "xmake default parse-cache build failed.\n${_default_parse_build_out}\n${_default_parse_build_err}")
endif()
get_filename_component(_default_parse_cache_dir "${_out_dir}/.gentest_codegen_parse_cache" ABSOLUTE)
set(_default_parse_build_log "${_default_parse_build_out}\n${_default_parse_build_err}")
string(FIND "${_default_parse_build_log}" "${_default_parse_cache_dir}" _default_parse_cache_dir_pos)
if(_default_parse_cache_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "An enabled Xmake parse cache with an empty directory did not use the build-owned default.\n"
    "Expected: ${_default_parse_cache_dir}\n${_default_parse_build_log}")
endif()

set(_generated_glob_root "${_out_dir}/gen/*/*/*")
if(WIN32)
  set(_generated_glob_root "${_out_dir}/g/*/*/*")
endif()

foreach(_expected_glob IN ITEMS
    "${_generated_glob_root}/consumer_textual_mocks/gentest_consumer_mocks.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_defs_input.cpp"
    "${_generated_glob_root}/consumer_textual_mocks/tu_0000_consumer_textual_mocks_defs.gentest.h"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_registry.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_impl.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_registry__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_impl__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_textual/tu_0000_cases.gentest.h"
    "${_generated_glob_root}/consumer_textual/gentest_consumer_textual_xmake.artifact_manifest.json")
  file(GLOB _expected_matches LIST_DIRECTORIES FALSE "${_expected_glob}")
  list(LENGTH _expected_matches _expected_match_count)
  if(NOT _expected_match_count EQUAL 1)
    message(FATAL_ERROR
      "xmake textual consumer build did not produce expected mock/codegen artifact '${_expected_glob}'.\n"
      "Matches:\n${_expected_matches}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

# A malformed sidecar must be a recoverable miss. Its old native dependency
# cache must not suppress codegen before the sidecar can be republished. The
# next unchanged build proves that the replacement is usable.
file(GLOB _sidecar_matches LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_textual/tu_0000_cases.gentest.h.gentest_codegen_deps.v.*")
list(LENGTH _sidecar_matches _sidecar_match_count)
if(NOT _sidecar_match_count EQUAL 1)
  message(FATAL_ERROR
    "Expected one textual Xmake codegen dependency sidecar, found ${_sidecar_match_count}.\n"
    "Matches:\n${_sidecar_matches}")
endif()
list(GET _sidecar_matches 0 _sidecar)
file(WRITE "${_sidecar}" "corrupt gentest Xmake sidecar\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _corrupt_sidecar_rc
  OUTPUT_VARIABLE _corrupt_sidecar_out
  ERROR_VARIABLE _corrupt_sidecar_err)
if(NOT _corrupt_sidecar_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build did not recover from a corrupt textual codegen sidecar.\n"
    "stdout:\n${_corrupt_sidecar_out}\n"
    "stderr:\n${_corrupt_sidecar_err}")
endif()
set(_corrupt_sidecar_log "${_corrupt_sidecar_out}\n${_corrupt_sidecar_err}")
string(FIND "${_corrupt_sidecar_log}" "--source-root" _corrupt_sidecar_codegen_pos)
if(_corrupt_sidecar_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "A corrupt textual codegen sidecar did not force gentest_codegen.\n"
    "stdout/stderr:\n${_corrupt_sidecar_log}")
endif()

file(GLOB _recovery_header_matches LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_textual/tu_0000_cases.gentest.h")
list(GET _recovery_header_matches 0 _recovery_header)
file(TIMESTAMP "${_recovery_header}" _recovery_header_mtime_before "%s" UTC)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _recovered_noop_rc
  OUTPUT_VARIABLE _recovered_noop_out
  ERROR_VARIABLE _recovered_noop_err)
if(NOT _recovered_noop_rc EQUAL 0)
  message(FATAL_ERROR "xmake no-op build failed after sidecar recovery.\n${_recovered_noop_out}\n${_recovered_noop_err}")
endif()
set(_recovered_noop_log "${_recovered_noop_out}\n${_recovered_noop_err}")
string(FIND "${_recovered_noop_log}" "--source-root" _recovered_noop_codegen_pos)
if(NOT _recovered_noop_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "A recovered textual sidecar did not suppress the subsequent unchanged codegen run.\n"
    "stdout/stderr:\n${_recovered_noop_log}")
endif()
file(TIMESTAMP "${_recovery_header}" _recovery_header_mtime_after "%s" UTC)
if(NOT "${_recovery_header_mtime_before}" STREQUAL "${_recovery_header_mtime_after}")
  message(FATAL_ERROR "An unchanged Xmake build changed a generated output timestamp.")
endif()

# The manifest is a textual codegen product too. Rename it in the temporary
# build tree and require a complete regeneration of the output set.
file(GLOB _manifest_matches LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_textual/gentest_consumer_textual_xmake.artifact_manifest.json")
list(LENGTH _manifest_matches _manifest_match_count)
if(NOT _manifest_match_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one textual artifact manifest before deletion.")
endif()
list(GET _manifest_matches 0 _manifest)
file(RENAME "${_manifest}" "${_manifest}.removed")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _manifest_restore_rc
  OUTPUT_VARIABLE _manifest_restore_out
  ERROR_VARIABLE _manifest_restore_err)
if(NOT _manifest_restore_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build did not restore a deleted textual artifact manifest.\n"
    "stdout:\n${_manifest_restore_out}\n"
    "stderr:\n${_manifest_restore_err}")
endif()
set(_manifest_restore_log "${_manifest_restore_out}\n${_manifest_restore_err}")
string(FIND "${_manifest_restore_log}" "--source-root" _manifest_restore_codegen_pos)
if(_manifest_restore_codegen_pos EQUAL -1 OR NOT EXISTS "${_manifest}")
  message(FATAL_ERROR
    "Deleting a textual artifact manifest did not regenerate the complete codegen output set.\n"
    "stdout/stderr:\n${_manifest_restore_log}")
endif()

file(GLOB _public_header_matches LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_textual_mocks/gentest_consumer_mocks.hpp")
list(GET _public_header_matches 0 _public_header)
file(READ "${_public_header}" _public_header_contents)
if(NOT _public_header_contents MATCHES "consumer_textual_mocks_defs_input\\.cpp")
  message(FATAL_ERROR "The textual mock public header did not include the aggregate definitions source.\n${_public_header_contents}")
endif()

file(GLOB _mock_impl_matches LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_impl__domain_0000_header.hpp")
list(GET _mock_impl_matches 0 _mock_impl)
file(READ "${_mock_impl}" _mock_impl_contents)
if(NOT _mock_impl_contents MATCHES "AdditionalService")
  message(FATAL_ERROR "The second textual mock definitions file was not processed by codegen.\n${_mock_impl_contents}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env} "GENTEST_NO_INCLUDE_SOURCES=1"
          "${_xmake}" ${_xmake_build_args} gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _removed_env_rc
  OUTPUT_VARIABLE _removed_env_out
  ERROR_VARIABLE _removed_env_err)
if(_removed_env_rc EQUAL 0)
  message(FATAL_ERROR
    "GENTEST_NO_INCLUDE_SOURCES unexpectedly reused cached Xmake generation.\n"
    "stdout:\n${_removed_env_out}\nstderr:\n${_removed_env_err}")
endif()
set(_removed_env_log "${_removed_env_out}\n${_removed_env_err}")
string(FIND "${_removed_env_log}" "GENTEST_NO_INCLUDE_SOURCES was removed" _removed_env_diagnostic_pos)
if(_removed_env_diagnostic_pos EQUAL -1)
  message(FATAL_ERROR
    "Changing GENTEST_NO_INCLUDE_SOURCES did not run codegen and surface the removal diagnostic.\n${_removed_env_log}")
endif()

file(GLOB_RECURSE _consumer_bins
  LIST_DIRECTORIES FALSE
  "${_out_dir}/gentest_consumer_textual_xmake"
  "${_out_dir}/gentest_consumer_textual_xmake.exe")
list(LENGTH _consumer_bins _consumer_bin_count)
if(NOT _consumer_bin_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one built Xmake textual consumer binary, found ${_consumer_bin_count}.\n"
    "Candidates:\n${_consumer_bins}")
endif()
list(GET _consumer_bins 0 _consumer_bin)

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer listing failed.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_expected IN ITEMS
    "consumer/consumer/module_test"
    "consumer/consumer/module_mock"
    "consumer/consumer/log_sink"
    "consumer/consumer/module_bench"
    "consumer/consumer/module_jitter")
  string(FIND "${_list_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "The Xmake textual consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_test --kind=test
  RESULT_VARIABLE _module_test_rc
  OUTPUT_VARIABLE _module_test_out
  ERROR_VARIABLE _module_test_err)
if(NOT _module_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer plain test case failed.\n"
    "stdout:\n${_module_test_out}\n"
    "stderr:\n${_module_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_mock --kind=test
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer mock case failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/log_sink --kind=test
  RESULT_VARIABLE _log_sink_rc
  OUTPUT_VARIABLE _log_sink_out
  ERROR_VARIABLE _log_sink_err)
if(NOT _log_sink_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer log sink case failed.\n"
    "stdout:\n${_log_sink_out}\n"
    "stderr:\n${_log_sink_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_bench --kind=bench
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer bench failed.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_jitter --kind=jitter
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
