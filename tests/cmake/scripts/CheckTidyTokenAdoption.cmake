# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckTidyTokenAdoption.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckTidyTokenAdoption.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckTidyTokenAdoption.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

function(_gentest_tidy_assert_contains text needle description)
  string(FIND "${text}" "${needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${needle}' in clang-tidy output:\n${text}")
  endif()
endfunction()

function(_gentest_tidy_tool_major out_var tool)
  execute_process(
    COMMAND "${tool}" --version
    RESULT_VARIABLE _version_rc
    OUTPUT_VARIABLE _version_out
    ERROR_VARIABLE _version_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  set(_version_text "${_version_out}\n${_version_err}")
  if(_version_rc EQUAL 0 AND _version_text MATCHES "version[ \t]+([0-9]+)")
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(_gentest_clang_resource_prefix out_var compiler)
  execute_process(
    COMMAND "${compiler}" -print-resource-dir
    RESULT_VARIABLE _resource_rc
    OUTPUT_VARIABLE _resource_out
    ERROR_VARIABLE _resource_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _resource_rc EQUAL 0 OR "${_resource_out}" STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_clang_dir "${_resource_out}" DIRECTORY)
  get_filename_component(_lib_dir "${_clang_dir}" DIRECTORY)
  get_filename_component(_prefix "${_lib_dir}" DIRECTORY)
  set(${out_var} "${_prefix}" PARENT_SCOPE)
endfunction()

function(_gentest_find_package_cmake_dir out_var prefix package_name)
  foreach(_candidate IN ITEMS
      "${prefix}/lib/cmake/${package_name}"
      "${prefix}/lib64/cmake/${package_name}")
    if(EXISTS "${_candidate}")
      set(${out_var} "${_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

set(_work_dir "${BUILD_ROOT}/tidy_token_adoption")
set(_plugin_build_dir "${_work_dir}/plugin")
set(_fixture_build_dir "${_work_dir}/fixture")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _ninja)
  gentest_skip_test("tidy token adoption regression: ${_ninja_reason}")
  return()
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("tidy token adoption regression: clang/clang++ not found")
  return()
endif()

_gentest_tidy_tool_major(_clangxx_major "${_clangxx}")

set(_tool_hints "")
foreach(_cmake_dir IN ITEMS "${LLVM_DIR}" "${Clang_DIR}")
  if(NOT "${_cmake_dir}" STREQUAL "")
    get_filename_component(_tool_prefix "${_cmake_dir}/../../.." ABSOLUTE)
    list(APPEND _tool_hints "${_tool_prefix}/bin")
  endif()
endforeach()
get_filename_component(_clangxx_bin_dir "${_clangxx}" DIRECTORY)
list(PREPEND _tool_hints "${_clangxx_bin_dir}")
list(REMOVE_DUPLICATES _tool_hints)
set(_clang_tidy_names)
if(NOT "${_clangxx_major}" STREQUAL "")
  if(CMAKE_HOST_WIN32)
    list(APPEND _clang_tidy_names "clang-tidy-${_clangxx_major}.exe" "clang-tidy-${_clangxx_major}")
  else()
    list(APPEND _clang_tidy_names "clang-tidy-${_clangxx_major}")
  endif()
endif()
list(APPEND _clang_tidy_names clang-tidy clang-tidy.exe)
find_program(_clang_tidy NAMES ${_clang_tidy_names} HINTS ${_tool_hints} NO_DEFAULT_PATH)
if(NOT _clang_tidy)
  gentest_skip_test("tidy token adoption regression: clang-tidy not found next to selected clang toolchain")
  return()
endif()
_gentest_tidy_tool_major(_clang_tidy_major "${_clang_tidy}")
if(NOT "${_clangxx_major}" STREQUAL "" AND NOT "${_clang_tidy_major}" STREQUAL "" AND NOT "${_clangxx_major}" STREQUAL "${_clang_tidy_major}")
  gentest_skip_test(
    "tidy token adoption regression: clang-tidy major ${_clang_tidy_major} does not match selected clang++ major ${_clangxx_major}")
  return()
endif()

set(_plugin_llvm_dir "${LLVM_DIR}")
set(_plugin_clang_dir "${Clang_DIR}")
if("${_plugin_llvm_dir}" STREQUAL "" OR "${_plugin_clang_dir}" STREQUAL "")
  _gentest_clang_resource_prefix(_clang_prefix "${_clangxx}")
  if(NOT "${_clang_prefix}" STREQUAL "")
    if("${_plugin_llvm_dir}" STREQUAL "")
      _gentest_find_package_cmake_dir(_plugin_llvm_dir "${_clang_prefix}" llvm)
    endif()
    if("${_plugin_clang_dir}" STREQUAL "")
      _gentest_find_package_cmake_dir(_plugin_clang_dir "${_clang_prefix}" clang)
    endif()
  endif()
endif()

set(_expected_llvm_major "${_clang_tidy_major}")
if("${_expected_llvm_major}" STREQUAL "")
  set(_expected_llvm_major "${_clangxx_major}")
endif()

set(_common_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(NOT "${_plugin_llvm_dir}" STREQUAL "")
  list(APPEND _common_cache_args "-DLLVM_DIR=${_plugin_llvm_dir}")
endif()
if(NOT "${_plugin_clang_dir}" STREQUAL "")
  list(APPEND _common_cache_args "-DClang_DIR=${_plugin_clang_dir}")
endif()
gentest_resolve_fixture_build_type(_effective_build_type "${_clangxx}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_common_cache_args "${_clangxx}" ${_common_cache_args})
gentest_append_host_apple_sysroot(_common_cache_args)

message(STATUS "Configure gentest clang-tidy plugin fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_plugin_build_dir}"
    ${_common_cache_args}
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=OFF"
    "-DGENTEST_BUILD_TIDY_PLUGIN=ON"
    "-DGENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR=${_expected_llvm_major}"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=OFF"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_plugin_ninja "${_plugin_build_dir}/build.ninja")
if(NOT EXISTS "${_plugin_ninja}")
  gentest_skip_test("tidy token adoption regression: plugin build did not generate Ninja files")
  return()
endif()
file(READ "${_plugin_ninja}" _plugin_ninja_text)
string(FIND "${_plugin_ninja_text}" "gentest_tidy" _plugin_target_pos)
if(_plugin_target_pos EQUAL -1)
  gentest_skip_test("tidy token adoption regression: clang-tidy plugin target is unavailable")
  return()
endif()

message(STATUS "Build gentest clang-tidy plugin fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_plugin_build_dir}" --target gentest_tidy
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB_RECURSE _plugin_candidates LIST_DIRECTORIES FALSE
  "${_plugin_build_dir}/*GentestTidyModule*.so"
  "${_plugin_build_dir}/*GentestTidyModule*.dylib"
  "${_plugin_build_dir}/*GentestTidyModule*.dll")
list(LENGTH _plugin_candidates _plugin_candidate_count)
if(_plugin_candidate_count EQUAL 0)
  message(FATAL_ERROR "Expected built GentestTidyModule plugin under '${_plugin_build_dir}'")
endif()
list(GET _plugin_candidates 0 _plugin_path)

message(STATUS "Configure tidy token adoption source fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${SOURCE_DIR}"
    -B "${_fixture_build_dir}"
    ${_common_cache_args}
    "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=OFF"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=OFF"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_tidy_common_args
  "--load=${_plugin_path}"
  "--checks=-*,gentest-token-adoption"
  "--config={}"
  "--header-filter=.*"
  "-p" "${_fixture_build_dir}")

execute_process(
  COMMAND
    "${_clang_tidy}"
    "--load=${_plugin_path}"
    "--checks=-*,gentest-token-adoption"
    "--list-checks"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _list_tidy_rc
  OUTPUT_VARIABLE _list_tidy_out
  ERROR_VARIABLE _list_tidy_err)
set(_list_tidy_text "${_list_tidy_out}\n${_list_tidy_err}")
string(FIND "${_list_tidy_text}" "gentest-token-adoption" _loaded_check_pos)
if(NOT _list_tidy_rc EQUAL 0 OR _loaded_check_pos EQUAL -1)
  gentest_skip_test("tidy token adoption regression: clang-tidy did not expose loaded gentest plugin checks")
  return()
endif()

message(STATUS "Run tidy token adoption negative fixture...")
execute_process(
  COMMAND
    "${_clang_tidy}"
    ${_tidy_common_args}
    "--warnings-as-errors=gentest-token-adoption"
    "${SOURCE_DIR}/bad_cases.cpp"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _bad_tidy_rc
  OUTPUT_VARIABLE _bad_tidy_out
  ERROR_VARIABLE _bad_tidy_err)
set(_bad_tidy_text "${_bad_tidy_out}\n${_bad_tidy_err}")
if(_bad_tidy_rc EQUAL 0)
  message(FATAL_ERROR "Expected gentest-token-adoption diagnostics for bad_cases.cpp, but clang-tidy succeeded:\n${_bad_tidy_text}")
endif()
_gentest_tidy_assert_contains("${_bad_tidy_text}" "gentest-token-adoption" "negative tidy fixture")
_gentest_tidy_assert_contains("${_bad_tidy_text}" "thread-like callback" "negative tidy fixture")
_gentest_tidy_assert_contains("${_bad_tidy_text}" "coroutine body" "negative tidy fixture")
_gentest_tidy_assert_contains("${_bad_tidy_text}" "gentest::set_current_token()" "negative tidy fixture")

message(STATUS "Run tidy token adoption positive fixture...")
execute_process(
  COMMAND
    "${_clang_tidy}"
    ${_tidy_common_args}
    "--warnings-as-errors=gentest-token-adoption"
    "${SOURCE_DIR}/good_cases.cpp"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _good_tidy_rc
  OUTPUT_VARIABLE _good_tidy_out
  ERROR_VARIABLE _good_tidy_err)
if(NOT _good_tidy_rc EQUAL 0)
  message(FATAL_ERROR
    "Expected no gentest-token-adoption diagnostics for good_cases.cpp.\n"
    "stdout:\n${_good_tidy_out}\n"
    "stderr:\n${_good_tidy_err}")
endif()

message(STATUS "Observed gentest-token-adoption tidy diagnostics")
