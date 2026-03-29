if(WIN32)
  message(STATUS "CheckCodegenExplicitHostClang.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExplicitHostClang.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExplicitHostClang.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExplicitHostClang.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if("${_clangxx}" STREQUAL "")
  gentest_skip_test("explicit host-clang regression: no clang++ compiler available")
  return()
endif()

get_filename_component(_clangxx_dir "${_clangxx}" DIRECTORY)
find_program(_clang_scan_deps
  NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20
  PATHS "${_clangxx_dir}"
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps
    NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20)
endif()
if(NOT _clang_scan_deps)
  gentest_skip_test("explicit host-clang regression: no clang-scan-deps executable available")
  return()
endif()

gentest_find_supported_ninja(_ninja _ninja_reason)
if("${_ninja}" STREQUAL "")
  gentest_skip_test("explicit host-clang regression: ${_ninja_reason}")
  return()
endif()

file(TO_CMAKE_PATH "${PROG}" _prog_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
file(TO_CMAKE_PATH "${_clangxx}" _clangxx_norm)

set(_work_dir "${BUILD_ROOT}/codegen_explicit_host_clang")
set(_module_dir "${_work_dir}/module_case")
set(_generated_default_dir "${_module_dir}/generated_default")
set(_generated_cli_dir "${_module_dir}/generated_cli")
set(_generated_env_dir "${_module_dir}/generated_env")
set(_empty_path_dir "${_module_dir}/empty-path")
set(_bin_dir "${_module_dir}/bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_generated_default_dir}" "${_generated_cli_dir}" "${_generated_env_dir}" "${_empty_path_dir}" "${_bin_dir}")

gentest_fixture_write_file("${_module_dir}/suite.cppm" [=[
export module gentest.hostclang.contract;
import gentest.hostclang.provider;
export int explicit_host_clang_value() { return provider_value(); }
]=])
gentest_fixture_write_file("${_module_dir}/provider.cppm" [=[
export module gentest.hostclang.provider;
export int provider_value() { return 7; }
]=])

gentest_fixture_write_file("${_bin_dir}/g++" [=[
#!/bin/sh
echo "fake-host-clang-fallback-should-not-run $*" >&2
exit 23
]=])
file(CHMOD "${_bin_dir}/g++"
  PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

file(TO_CMAKE_PATH "${_module_dir}" _module_dir_norm)
file(TO_CMAKE_PATH "${_module_dir}/suite.cppm" _suite_source_abs)
file(TO_CMAKE_PATH "${_module_dir}/provider.cppm" _provider_source_abs)

set(_common_args
  "clang++"
  "-std=c++20"
  "-I${_source_dir_norm}/include")
if(CMAKE_HOST_APPLE)
  set(_sdkroot "$ENV{SDKROOT}")
  if("${_sdkroot}" STREQUAL "")
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      RESULT_VARIABLE _xcrun_rc
      OUTPUT_VARIABLE _xcrun_out
      ERROR_VARIABLE _xcrun_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE)
    if(_xcrun_rc EQUAL 0 AND NOT "${_xcrun_out}" STREQUAL "")
      set(_sdkroot "${_xcrun_out}")
    endif()
  endif()
  if(NOT "${_sdkroot}" STREQUAL "")
    list(APPEND _common_args "-isysroot" "${_sdkroot}")
  endif()
endif()

gentest_fixture_make_compdb_entry(_suite_entry
  DIRECTORY "${_module_dir_norm}"
  FILE "suite.cppm"
  ARGUMENTS ${_common_args} "-c" "suite.cppm" "-o" "suite.o")
gentest_fixture_make_compdb_entry(_provider_entry
  DIRECTORY "${_module_dir_norm}"
  FILE "provider.cppm"
  ARGUMENTS ${_common_args} "-c" "provider.cppm" "-o" "provider.o")
gentest_fixture_write_compdb("${_module_dir}/compile_commands.json" "${_suite_entry}" "${_provider_entry}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_empty_path_dir}"
    "CC=${_bin_dir}/g++"
    "CXX=${_bin_dir}/g++"
    "${PROG}" --check --scan-deps-mode=ON --clang-scan-deps "${_clang_scan_deps}" --external-module-source "gentest.hostclang.provider=${_provider_source_abs}" --compdb "${_module_dir}" --tu-out-dir "${_generated_default_dir}" "${_suite_source_abs}"
  WORKING_DIRECTORY "${_module_dir}"
  RESULT_VARIABLE _default_rc
  OUTPUT_VARIABLE _default_out
  ERROR_VARIABLE _default_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_default_rc EQUAL 0)
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen should fail without an explicit host clang when bare clang++ cannot be resolved and CXX is non-clang.\n"
    "Output:\n${_default_out}\nErrors:\n${_default_err}")
endif()
if(_default_err MATCHES "fake-host-clang-fallback-should-not-run")
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen incorrectly tried to use the non-clang CXX fallback.\n"
    "Output:\n${_default_out}\nErrors:\n${_default_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_empty_path_dir}"
    "CC=${_bin_dir}/g++"
    "CXX=${_bin_dir}/g++"
    "${PROG}" --check --scan-deps-mode=ON --clang-scan-deps "${_clang_scan_deps}" --host-clang "${_clangxx_norm}" --external-module-source "gentest.hostclang.provider=${_provider_source_abs}" --compdb "${_module_dir}" --tu-out-dir "${_generated_cli_dir}" "${_suite_source_abs}"
  WORKING_DIRECTORY "${_module_dir}"
  RESULT_VARIABLE _cli_rc
  OUTPUT_VARIABLE _cli_out
  ERROR_VARIABLE _cli_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _cli_rc EQUAL 0)
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen failed with --host-clang.\n"
    "Output:\n${_cli_out}\nErrors:\n${_cli_err}")
endif()
if(_cli_err MATCHES "fake-host-clang-fallback-should-not-run")
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen still tried to use the non-clang CXX fallback with --host-clang.\n"
    "Output:\n${_cli_out}\nErrors:\n${_cli_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_empty_path_dir}"
    "CC=${_bin_dir}/g++"
    "CXX=${_bin_dir}/g++"
    "GENTEST_CODEGEN_HOST_CLANG=${_clangxx_norm}"
    "${PROG}" --check --scan-deps-mode=ON --clang-scan-deps "${_clang_scan_deps}" --external-module-source "gentest.hostclang.provider=${_provider_source_abs}" --compdb "${_module_dir}" --tu-out-dir "${_generated_env_dir}" "${_suite_source_abs}"
  WORKING_DIRECTORY "${_module_dir}"
  RESULT_VARIABLE _env_rc
  OUTPUT_VARIABLE _env_out
  ERROR_VARIABLE _env_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _env_rc EQUAL 0)
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen failed with GENTEST_CODEGEN_HOST_CLANG.\n"
    "Output:\n${_env_out}\nErrors:\n${_env_err}")
endif()
if(_env_err MATCHES "fake-host-clang-fallback-should-not-run")
  message(FATAL_ERROR
    "explicit host-clang regression: gentest_codegen still tried to use the non-clang CXX fallback with GENTEST_CODEGEN_HOST_CLANG.\n"
    "Output:\n${_env_out}\nErrors:\n${_env_err}")
endif()

set(_cmake_project_dir "${_work_dir}/cmake_fixture")
set(_cmake_build_dir "${_work_dir}/cmake_fixture_build")
file(MAKE_DIRECTORY "${_cmake_project_dir}")

gentest_fixture_write_file("${_cmake_project_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_host_clang_contract LANGUAGES C CXX)

include("]=] "${_source_dir_norm}" [=[/cmake/GentestCodegen.cmake")

set(GENTEST_CODEGEN_EXECUTABLE "]=] "${_prog_norm}" [=[" CACHE FILEPATH "" FORCE)
set(GENTEST_CODEGEN_HOST_CLANG "]=] "${_clangxx_norm}" [=[" CACHE FILEPATH "" FORCE)

add_executable(host_clang_consumer main.cpp cases.cpp)
gentest_attach_codegen(host_clang_consumer
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/host_clang_consumer.gentest.cpp")
]=])
gentest_fixture_write_file("${_cmake_project_dir}/main.cpp" "int main() { return 0; }\n")
gentest_fixture_write_file("${_cmake_project_dir}/cases.cpp" "void host_clang_consumer_case() {}\n")

set(_configure_cmd
  "${CMAKE_COMMAND}"
  -S "${_cmake_project_dir}"
  -B "${_cmake_build_dir}"
  -G Ninja
  "-DCMAKE_MAKE_PROGRAM=${_ninja}")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _configure_cmd "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _configure_cmd "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _configure_cmd "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_host_apple_sysroot(_configure_cmd)

execute_process(
  COMMAND ${_configure_cmd}
  RESULT_VARIABLE _configure_rc
  OUTPUT_VARIABLE _configure_out
  ERROR_VARIABLE _configure_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _configure_rc EQUAL 0)
  message(FATAL_ERROR
    "explicit host-clang regression: failed to configure the minimal GentestCodegen.cmake fixture.\n"
    "Output:\n${_configure_out}\nErrors:\n${_configure_err}")
endif()

file(READ "${_cmake_build_dir}/build.ninja" _build_ninja)
string(FIND "${_build_ninja}" "--host-clang" _host_clang_flag_pos)
if(_host_clang_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "explicit host-clang regression: GentestCodegen.cmake did not forward --host-clang into the generated build command.\n"
    "build.ninja:\n${_build_ninja}")
endif()
string(FIND "${_build_ninja}" "${_clangxx_norm}" _host_clang_path_pos)
if(_host_clang_path_pos EQUAL -1)
  message(FATAL_ERROR
    "explicit host-clang regression: GentestCodegen.cmake did not forward the configured host clang path.\n"
    "Expected path: ${_clangxx_norm}\n"
    "build.ninja:\n${_build_ninja}")
endif()
