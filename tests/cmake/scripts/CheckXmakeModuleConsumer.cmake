if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: PROG not set")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/ModuleArtifactManifestAssertions.cmake")

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

function(_gentest_resolve_xmake_test_tool out_var raw_value label)
  if("${raw_value}" STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  if(IS_ABSOLUTE "${raw_value}")
    if(NOT EXISTS "${raw_value}")
      message(FATAL_ERROR
        "CheckXmakeModuleConsumer.cmake: ${label} does not exist: ${raw_value}")
    endif()
    set(${out_var} "${raw_value}" PARENT_SCOPE)
    return()
  endif()

  find_program(_resolved_tool NAMES "${raw_value}")
  if(NOT _resolved_tool)
    message(FATAL_ERROR
      "CheckXmakeModuleConsumer.cmake: failed to resolve ${label} from '${raw_value}'.")
  endif()
  set(${out_var} "${_resolved_tool}" PARENT_SCOPE)
endfunction()

function(_gentest_prepare_xmake_workspace out_var source_dir workspace_root)
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
  message(STATUS "xmake not found; skipping Xmake module consumer smoke check.")
  return()
endif()

set(_gentest_xmake_root "${CMAKE_CURRENT_BINARY_DIR}")
if(DEFINED BUILD_ROOT AND NOT "${BUILD_ROOT}" STREQUAL "")
  set(_gentest_xmake_root "${BUILD_ROOT}")
endif()

_gentest_prepare_xmake_workspace(_project_dir "${SOURCE_DIR}" "${_gentest_xmake_root}")

set(_module_mock_probe_source "${_project_dir}/tests/consumer/module_mock_defs.cppm")
file(READ "${_module_mock_probe_source}" _module_mock_probe_text)
set(_module_mock_probe_fragment
  "#if __has_include(\"xmake_module_mock_optional.hpp\")\n#include \"xmake_module_mock_optional.hpp\"\n#endif\n")
string(FIND "${_module_mock_probe_text}" "module;\n" _module_mock_fragment_pos)
if(_module_mock_fragment_pos EQUAL -1)
  set(_module_mock_probe_text "module;\n${_module_mock_probe_fragment}${_module_mock_probe_text}")
else()
  string(REPLACE
    "module;\n"
    "module;\n${_module_mock_probe_fragment}"
    _module_mock_probe_text
    "${_module_mock_probe_text}")
endif()
file(WRITE "${_module_mock_probe_source}" "${_module_mock_probe_text}")

set(_module_suite_probe_source "${_project_dir}/tests/consumer/cases.cppm")
file(READ "${_module_suite_probe_source}" _module_suite_probe_text)
string(REPLACE
  "#include <string_view>\n"
  "#include <string_view>\n#if __has_include(\"xmake_module_suite_optional.hpp\")\n#include \"xmake_module_suite_optional.hpp\"\n#endif\n"
  _module_suite_probe_text
  "${_module_suite_probe_text}")
file(WRITE "${_module_suite_probe_source}" "${_module_suite_probe_text}")

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
    "CheckXmakeModuleConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
if(_has_configured_target_cxx AND NOT _has_configured_target_cc)
  message(FATAL_ERROR
    "CheckXmakeModuleConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
set(_nonclang_target_cc "")
set(_nonclang_target_cxx "")
if(NOT "${_configured_target_cc}" STREQUAL "")
  _gentest_resolve_xmake_test_tool(_nonclang_target_cc "${_configured_target_cc}" "GENTEST_XMAKE_TEST_TARGET_CC")
  _gentest_resolve_xmake_test_tool(_nonclang_target_cxx "${_configured_target_cxx}" "GENTEST_XMAKE_TEST_TARGET_CXX")
endif()

if(NOT WIN32)
  if(NOT "${_nonclang_target_cxx}" STREQUAL "" AND NOT "${_nonclang_target_cc}" STREQUAL "")
    execute_process(COMMAND "${_nonclang_target_cxx}" --version OUTPUT_VARIABLE _nonclang_cxx_out ERROR_VARIABLE _nonclang_cxx_err)
    execute_process(COMMAND "${_nonclang_target_cc}" --version OUTPUT_VARIABLE _nonclang_cc_out ERROR_VARIABLE _nonclang_cc_err)
    string(TOLOWER "${_nonclang_cxx_out}\n${_nonclang_cxx_err}" _nonclang_cxx_log)
    string(TOLOWER "${_nonclang_cc_out}\n${_nonclang_cc_err}" _nonclang_cc_log)
    if(NOT _nonclang_cxx_log MATCHES "clang" AND NOT _nonclang_cc_log MATCHES "clang")
      set(_nonclang_out_dir "${_gentest_xmake_root}/tmp_xmake_module_consumer_nonclang")
      set(_nonclang_xmake_global_dir "${_gentest_xmake_root}/xg_nonclang")
      file(REMOVE_RECURSE "${_nonclang_out_dir}")
      file(REMOVE_RECURSE "${_nonclang_xmake_global_dir}")
      file(MAKE_DIRECTORY "${_nonclang_out_dir}/tmp")
      # Keep user-global Xmake cache/toolchain state out of the negative contract check.
      set(_nonclang_xmake_env
        "GENTEST_CODEGEN=${_codegen}"
        "CC=${_nonclang_target_cc}"
        "CXX=${_nonclang_target_cxx}"
        "TMPDIR=${_nonclang_out_dir}/tmp"
        "XMAKE_GLOBALDIR=${_nonclang_xmake_global_dir}")
      set(_nonclang_config_args
        f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_nonclang_out_dir}" -m debug -c -y
        "--cc=${_nonclang_target_cc}"
        "--cxx=${_nonclang_target_cxx}")
      execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${_nonclang_xmake_env}
                "${_xmake}" ${_nonclang_config_args}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE _nonclang_cfg_rc
        OUTPUT_VARIABLE _nonclang_cfg_out
        ERROR_VARIABLE _nonclang_cfg_err)
      if(_nonclang_cfg_rc EQUAL 0)
        message(FATAL_ERROR
          "Expected Xmake module configure with a non-Clang toolchain to fail, but it succeeded.\n"
          "stdout:\n${_nonclang_cfg_out}\n"
          "stderr:\n${_nonclang_cfg_err}")
      endif()

      set(_nonclang_cfg_log "${_nonclang_cfg_out}\n${_nonclang_cfg_err}")
      string(FIND "${_nonclang_cfg_log}" "requires a Clang C++ target toolchain in Xmake" _nonclang_contract_pos)
      if(_nonclang_contract_pos EQUAL -1)
        message(FATAL_ERROR
          "Xmake module configure with a non-Clang toolchain failed without surfacing the documented helper contract.\n"
          "stdout:\n${_nonclang_cfg_out}\n"
          "stderr:\n${_nonclang_cfg_err}")
      endif()
    else()
      message(STATUS
        "GENTEST_XMAKE_TEST_TARGET_CC/CXX resolve to a clang-like toolchain; skipping non-Clang Xmake module contract check.")
    endif()
  else()
    message(STATUS
      "GENTEST_XMAKE_TEST_TARGET_CC/CXX not set; skipping non-Clang Xmake module contract check.")
  endif()
endif()

execute_process(COMMAND "${_xmake}" --version OUTPUT_VARIABLE _xmake_version_out ERROR_VARIABLE _xmake_version_err RESULT_VARIABLE _xmake_version_rc)
if(NOT _xmake_version_rc EQUAL 0)
  message(STATUS "failed to query xmake version; skipping Xmake module consumer smoke check.")
  return()
endif()
string(REGEX MATCH "xmake v([0-9]+\\.[0-9]+\\.[0-9]+)" _xmake_version_match "${_xmake_version_out}")
set(_xmake_version "${CMAKE_MATCH_1}")
if(_xmake_version STREQUAL "" OR _xmake_version VERSION_LESS "3.0.6")
  message(STATUS "xmake ${_xmake_version} is older than 3.0.6; skipping Xmake module consumer smoke check.")
  return()
endif()

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
  message(STATUS "clang++ not found; skipping Xmake module consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake module consumer smoke check.")
  return()
endif()

find_program(_clang_scan_deps NAMES clang-scan-deps-23 clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20 clang-scan-deps-19 clang-scan-deps
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps NAMES clang-scan-deps-23 clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20 clang-scan-deps-19 clang-scan-deps)
endif()

set(_out_dir "${_gentest_xmake_root}/tmp_xmake_module_consumer")
set(_xmake_global_dir "${_gentest_xmake_root}/xg")
if(WIN32)
  set(_out_dir "${_gentest_xmake_root}/b")
endif()
file(REMOVE_RECURSE "${_out_dir}")
file(REMOVE_RECURSE "${_xmake_global_dir}")
file(MAKE_DIRECTORY "${_out_dir}/tmp")

get_filename_component(_clang_bin_dir "${_clang_cxx}" DIRECTORY)
get_filename_component(_clang_prefix "${_clang_bin_dir}" DIRECTORY)

set(_xmake_env
  "GENTEST_CODEGEN=${_codegen}"
  "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
  "CC=${_clang_cc}"
  "CXX=${_clang_cxx}"
  "TMPDIR=${_out_dir}/tmp"
  "XMAKE_GLOBALDIR=${_xmake_global_dir}")
if(_clang_scan_deps)
  list(APPEND _xmake_env "GENTEST_CODEGEN_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()

set(_clang_config_args
  f -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  "--cc=${_clang_cc}"
  "--cxx=${_clang_cxx}")
set(_clang_build_args
  build -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -y -vD)
if(APPLE OR WIN32)
  if(WIN32)
    set(_clang_config_args
      f -P . -F xmake.lua -o "${_out_dir}" -m debug -c -y
      "--cc=${_clang_cc}"
      "--cxx=${_clang_cxx}")
    set(_clang_build_args
      build -P . -F xmake.lua -y -vD)
  endif()
  list(APPEND _clang_config_args "--toolchain=llvm")
endif()
if(APPLE)
  list(APPEND _clang_config_args "--sdk=${_clang_prefix}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_config_args}
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE _cfg_err)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake configure failed for the module consumer smoke check.\n"
    "stdout:\n${_cfg_out}\n"
    "stderr:\n${_cfg_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_build_args}
          gentest_consumer_module_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _mock_build_rc
  OUTPUT_VARIABLE _mock_build_out
  ERROR_VARIABLE _mock_build_err)
if(NOT _mock_build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_module_mocks_xmake.\n"
    "stdout:\n${_mock_build_out}\n"
    "stderr:\n${_mock_build_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_build_args}
          gentest_consumer_module_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_module_xmake.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

set(_build_log "${_mock_build_out}\n${_mock_build_err}\n${_build_out}\n${_build_err}")
string(FIND "${_build_log}" "--host-clang" _host_clang_flag_pos)
string(FIND "${_build_log}" "${_clang_cxx}" _host_clang_path_pos)
if(_host_clang_flag_pos EQUAL -1 OR _host_clang_path_pos EQUAL -1)
  message(FATAL_ERROR
    "xmake module consumer build did not forward the explicit host clang path to gentest_codegen.\n"
    "Expected host clang: ${_clang_cxx}\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()
if(_clang_scan_deps)
  string(FIND "${_build_log}" "--clang-scan-deps" _scan_deps_flag_pos)
  string(FIND "${_build_log}" "${_clang_scan_deps}" _scan_deps_path_pos)
  if(_scan_deps_flag_pos EQUAL -1 OR _scan_deps_path_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module consumer build did not forward the explicit clang-scan-deps path to gentest_codegen.\n"
      "Expected clang-scan-deps: ${_clang_scan_deps}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endif()
set(_mock_build_log "${_mock_build_out}\n${_mock_build_err}")
set(_suite_build_log "${_build_out}\n${_build_err}")
string(FIND "${_mock_build_log}" "--compdb" _mock_compdb_flag_pos)
if(_mock_compdb_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "xmake module mock codegen did not forward --compdb.\n"
    "stdout:\n${_mock_build_out}\n"
    "stderr:\n${_mock_build_err}")
endif()
string(FIND "${_suite_build_log}" "--compdb" _suite_compdb_flag_pos)
if(_suite_compdb_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "xmake module suite codegen did not forward --compdb.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()
foreach(_lookup_guard_log IN ITEMS _mock_build_log _suite_build_log)
  string(FIND "${${_lookup_guard_log}}" "--lookup-guard-output" _lookup_guard_flag_pos)
  if(_lookup_guard_flag_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module codegen did not request an exact lookup-guard sidecar.\n${${_lookup_guard_log}}")
  endif()
endforeach()
foreach(_expected IN ITEMS
    "-DGENTEST_CONSUMER_USE_MODULES=1"
    "-DGENTEST_XMAKE_MODULE_MOCKS_DEFINE=1"
    "-DGENTEST_XMAKE_MODULE_MOCKS_CODEGEN=1"
    "-DGENTEST_XMAKE_MODULE_CONSUMER_DEFINE=1"
    "-DGENTEST_XMAKE_MODULE_CONSUMER_CODEGEN=1")
  string(FIND "${_build_log}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module consumer build did not pass through expected codegen flag '${_expected}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

set(_generated_glob_root "${_out_dir}/gen/*/*/*")
if(WIN32)
  set(_generated_glob_root "${_out_dir}/g/*/*/*")
endif()

foreach(_expected_glob IN ITEMS
    "${_generated_glob_root}/consumer_module_mocks/gentest/consumer_mocks.cppm"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_registry.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_impl.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_registry__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_impl__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_registry__domain_0001_gentest_consumer_service.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_impl__domain_0001_gentest_consumer_service.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_registry__domain_0002_gentest_consumer_mock_defs.hpp"
    "${_generated_glob_root}/consumer_module_mocks/consumer_module_mocks_mock_impl__domain_0002_gentest_consumer_mock_defs.hpp"
    "${_generated_glob_root}/consumer_module_mocks/tu_0000_service_module.gentest.h"
    "${_generated_glob_root}/consumer_module_mocks/tu_0000_service_module.module.gentest.cppm"
    "${_generated_glob_root}/consumer_module_mocks/tu_0001_module_mock_defs.gentest.h"
    "${_generated_glob_root}/consumer_module_mocks/tu_0001_module_mock_defs.module.gentest.cppm"
    "${_generated_glob_root}/consumer_module/tu_0000_cases.gentest.h"
    "${_generated_glob_root}/consumer_module/tu_0000_cases.registration.gentest.cpp"
    "${_generated_glob_root}/consumer_module/gentest_consumer_module_xmake.artifact_manifest.json")
  file(GLOB _expected_matches LIST_DIRECTORIES FALSE "${_expected_glob}")
  list(LENGTH _expected_matches _expected_match_count)
  if(NOT _expected_match_count EQUAL 1)
    message(FATAL_ERROR
      "xmake module consumer build did not produce expected mock/codegen artifact '${_expected_glob}'.\n"
      "Matches:\n${_expected_matches}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

file(GLOB _aggregate_module_matches
  LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_module_mocks/gentest/consumer_mocks.cppm")
list(LENGTH _aggregate_module_matches _aggregate_module_count)
if(NOT _aggregate_module_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one Xmake aggregate module, found ${_aggregate_module_count}: ${_aggregate_module_matches}")
endif()
list(GET _aggregate_module_matches 0 _aggregate_module)
file(TIMESTAMP "${_aggregate_module}" _aggregate_mtime_before "%s.%f" UTC)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_build_args}
          gentest_consumer_module_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _repeat_build_rc
  OUTPUT_VARIABLE _repeat_build_out
  ERROR_VARIABLE _repeat_build_err)
if(NOT _repeat_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Immediate repeated Xmake module consumer build failed.\n"
    "stdout:\n${_repeat_build_out}\n"
    "stderr:\n${_repeat_build_err}")
endif()
file(TIMESTAMP "${_aggregate_module}" _aggregate_mtime_after "%s.%f" UTC)
if(NOT _aggregate_mtime_after STREQUAL _aggregate_mtime_before)
  message(FATAL_ERROR
    "Immediate repeated Xmake module build rewrote an unchanged aggregate.\n"
    "Before: ${_aggregate_mtime_before}\n"
    "After:  ${_aggregate_mtime_after}")
endif()

# Module mock and suite codegen must both retain wholly missing __has_include
# candidates. Creating each candidate schedules its owner once, then the
# stable membership produces a native Xmake no-op.
function(_gentest_check_module_optional_probe header_name target_name label)
  file(WRITE "${_project_dir}/tests/consumer/${header_name}" "#pragma once\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
            "${_xmake}" ${_clang_build_args} "${target_name}"
    WORKING_DIRECTORY "${_project_dir}"
    RESULT_VARIABLE _probe_rc
    OUTPUT_VARIABLE _probe_out
    ERROR_VARIABLE _probe_err)
  set(_probe_log "${_probe_out}\n${_probe_err}")
  string(FIND "${_probe_log}" "gentest-codegen-cache-miss" _probe_codegen_pos)
  if(NOT _probe_rc EQUAL 0 OR _probe_codegen_pos EQUAL -1)
    message(FATAL_ERROR
      "Creating the missing ${label} __has_include candidate did not rerun module codegen.\n${_probe_log}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
            "${_xmake}" ${_clang_build_args} "${target_name}"
    WORKING_DIRECTORY "${_project_dir}"
    RESULT_VARIABLE _probe_noop_rc
    OUTPUT_VARIABLE _probe_noop_out
    ERROR_VARIABLE _probe_noop_err)
  set(_probe_noop_log "${_probe_noop_out}\n${_probe_noop_err}")
  string(FIND "${_probe_noop_log}" "gentest-codegen-cache-miss" _probe_noop_codegen_pos)
  if(NOT _probe_noop_rc EQUAL 0 OR NOT _probe_noop_codegen_pos EQUAL -1)
    message(FATAL_ERROR "Stable ${label} __has_include membership did not produce an Xmake no-op.\n${_probe_noop_log}")
  endif()
endfunction()

_gentest_check_module_optional_probe(
  "xmake_module_mock_optional.hpp"
  "gentest_consumer_module_mocks_xmake"
  "module mock")
_gentest_check_module_optional_probe(
  "xmake_module_suite_optional.hpp"
  "gentest_consumer_module_xmake"
  "module suite")

set(_extra_defs "${_project_dir}/tests/consumer/module_mock_extra_defs.cppm")
file(WRITE "${_extra_defs}" [=[export module gentest.consumer_mock_extra_defs;

export import gentest.mock;
export import gentest.consumer_service;

export namespace consumer::mocks {

using ExtraServiceMock = gentest::mock<consumer::Service>;

} // namespace consumer::mocks
]=])
file(READ "${_project_dir}/xmake.lua" _xmake_project_content)
set(_defs_before
  [[defs = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"}]])
set(_defs_after
  [[defs = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm", "tests/consumer/module_mock_extra_defs.cppm"}]])
set(_modules_before
  [[defs_modules = {"gentest.consumer_service", "gentest.consumer_mock_defs"}]])
set(_modules_after
  [[defs_modules = {"gentest.consumer_service", "gentest.consumer_mock_defs", "gentest.consumer_mock_extra_defs"}]])
set(_headers_before
  [[headerfiles = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"}]])
set(_headers_after
  [[headerfiles = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm", "tests/consumer/module_mock_extra_defs.cppm"}]])
set(_mock_target_before [[target("gentest_consumer_module_mocks_xmake")]])
set(_mock_target_after [[target("regression::gentest_consumer_module_mocks_xmake")]])
set(_consumer_target_before [[target("gentest_consumer_module_xmake")]])
set(_consumer_target_after [[target("regression::gentest_consumer_module_xmake")]])
foreach(_replacement IN ITEMS defs modules headers mock_target consumer_target)
  string(FIND "${_xmake_project_content}" "${_${_replacement}_before}" _replacement_pos)
  if(_replacement_pos EQUAL -1)
    message(FATAL_ERROR
      "Failed to locate ${_replacement} list while preparing the changed Xmake module-input regression.")
  endif()
  string(REPLACE "${_${_replacement}_before}" "${_${_replacement}_after}"
    _xmake_project_content "${_xmake_project_content}")
endforeach()
file(WRITE "${_project_dir}/xmake.lua" "${_xmake_project_content}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_config_args}
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _changed_cfg_rc
  OUTPUT_VARIABLE _changed_cfg_out
  ERROR_VARIABLE _changed_cfg_err)
if(NOT _changed_cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "Xmake reconfigure failed for the changed module-input regression.\n"
    "stdout:\n${_changed_cfg_out}\n"
    "stderr:\n${_changed_cfg_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_build_args}
          regression::gentest_consumer_module_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _changed_mock_build_rc
  OUTPUT_VARIABLE _changed_mock_build_out
  ERROR_VARIABLE _changed_mock_build_err)
if(NOT _changed_mock_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Xmake mock build failed after changing the module mock input set.\n"
    "stdout:\n${_changed_mock_build_out}\n"
    "stderr:\n${_changed_mock_build_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_build_args}
          regression::gentest_consumer_module_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _changed_consumer_build_rc
  OUTPUT_VARIABLE _changed_consumer_build_out
  ERROR_VARIABLE _changed_consumer_build_err)
if(NOT _changed_consumer_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Xmake consumer build failed after changing the module mock input set.\n"
    "stdout:\n${_changed_consumer_build_out}\n"
    "stderr:\n${_changed_consumer_build_err}")
endif()
file(READ "${_aggregate_module}" _changed_aggregate_content)
string(FIND "${_changed_aggregate_content}"
  "export import gentest.consumer_mock_extra_defs;" _extra_import_pos)
if(_extra_import_pos EQUAL -1)
  message(FATAL_ERROR
    "Changed Xmake module mock input did not update the aggregate module.\n"
    "Aggregate:\n${_changed_aggregate_content}")
endif()

file(GLOB _module_manifest_matches
  LIST_DIRECTORIES FALSE
  "${_generated_glob_root}/consumer_module/gentest_consumer_module_xmake.artifact_manifest.json")
list(LENGTH _module_manifest_matches _module_manifest_count)
if(NOT _module_manifest_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one Xmake module artifact manifest, found ${_module_manifest_count}.\n"
    "Matches:\n${_module_manifest_matches}")
endif()
list(GET _module_manifest_matches 0 _module_manifest)
gentest_expect_module_artifact_manifest(
  "${_module_manifest}"
  "gentest.consumer_cases"
  "gentest_consumer_module_xmake:"
  "cases.cppm"
  "consumer_module/tu_0000_cases.registration.gentest.cpp")

file(GLOB_RECURSE _consumer_bins
  LIST_DIRECTORIES FALSE
  "${_out_dir}/gentest_consumer_module_xmake"
  "${_out_dir}/gentest_consumer_module_xmake.exe")
set(_namespaced_consumer_bins "")
foreach(_candidate IN LISTS _consumer_bins)
  file(TO_CMAKE_PATH "${_candidate}" _candidate_normalized)
  if(_candidate_normalized MATCHES "/regression/gentest_consumer_module_xmake(\\.exe)?$")
    list(APPEND _namespaced_consumer_bins "${_candidate}")
  endif()
endforeach()
list(LENGTH _namespaced_consumer_bins _consumer_bin_count)
if(NOT _consumer_bin_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one built namespaced Xmake module consumer binary, found ${_consumer_bin_count}.\n"
    "Candidates:\n${_consumer_bins}")
endif()
list(GET _namespaced_consumer_bins 0 _consumer_bin)

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer listing failed.\n"
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
      "The Xmake module consumer listing is missing '${_expected}'.\n"
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
    "Running the Xmake module consumer plain test case failed.\n"
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
    "Running the Xmake module consumer mock case failed.\n"
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
    "Running the Xmake module consumer log sink case failed.\n"
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
    "Running the Xmake module consumer bench failed.\n"
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
    "Running the Xmake module consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
