# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenPublicModuleImports.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenPublicModuleImports.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenPublicModuleImports.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/cgpmi")
set(_producer_build_dir "${_work_dir}/p")
set(_consumer_build_dir "${_work_dir}/consumer")
set(_consumer_missing_build_dir "${_work_dir}/missing")
set(_install_prefix "${_work_dir}/i")
set(_consumer_source_dir "${GENTEST_SOURCE_DIR}/tests/consumer")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("codegen public module imports regression: clang/clang++ not found")
  return()
endif()

gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _ninja)
  gentest_skip_test("codegen public module imports regression: ${_ninja_reason}")
  return()
endif()

gentest_find_clang_scan_deps(_scan_deps "${_clangxx}")
if(NOT _scan_deps)
  gentest_skip_test("codegen public module imports regression: clang-scan-deps not found")
  return()
endif()

set(_common_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_scan_deps}")
if(CMAKE_HOST_WIN32)
  list(APPEND _common_cache_args "-DCMAKE_OBJECT_PATH_MAX=160")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _common_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _common_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
gentest_resolve_fixture_build_type(_effective_build_type "${_clangxx}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_common_cache_args "${_clangxx}" ${_common_cache_args})
gentest_append_host_apple_sysroot(_common_cache_args)

message(STATUS "Configure producer for required module dependency scanning...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=ON"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=ON"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_assert_windows_native_llvm_cache_args(
  "${_producer_build_dir}" "${_clangxx}" "required module dependency scanning producer")

message(STATUS "Build and install producer...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

function(_gentest_configure_consumer build_dir)
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      -G Ninja
      -S "${_consumer_source_dir}"
      -B "${build_dir}"
      ${_common_cache_args}
      "-DCMAKE_PREFIX_PATH=${_install_prefix}"
      "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
      "-DGENTEST_CONSUMER_USE_MODULES=ON"
      "-DGENTEST_CONSUMER_LINK_MODE=double"
      ${ARGN}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  gentest_assert_windows_native_llvm_cache_args(
    "${build_dir}" "${_clangxx}" "required module dependency scanning consumer")
endfunction()

function(_gentest_try_read_windows_launcher command_text build_dir out_var)
  set(_launcher_path "")
  if("${command_text}" MATCHES "^\"([^\"]+\\.bat)\"( .*)?$")
    set(_launcher_path "${CMAKE_MATCH_1}")
  elseif("${command_text}" MATCHES "^([^\" ]+\\.bat)( .*)?$")
    set(_launcher_path "${CMAKE_MATCH_1}")
  elseif("${command_text}" MATCHES "^[Cc][Mm][Dd](\\.exe)? /[Cc] \"([^\"]+\\.bat)( [^\"]*)?\"$")
    set(_launcher_path "${CMAKE_MATCH_2}")
  endif()

  if(_launcher_path STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  if(NOT IS_ABSOLUTE "${_launcher_path}")
    set(_launcher_path "${build_dir}/${_launcher_path}")
  endif()
  cmake_path(NORMAL_PATH _launcher_path)
  if(NOT EXISTS "${_launcher_path}")
    message(FATAL_ERROR "Expected Windows gentest_codegen launcher at '${_launcher_path}'")
  endif()
  file(READ "${_launcher_path}" _launcher_text)
  set(${out_var} "${_launcher_text}" PARENT_SCOPE)
endfunction()

function(_gentest_extract_codegen_command build_dir out_var)
  set(_build_ninja "${build_dir}/build.ninja")
  file(READ "${_build_ninja}" _build_ninja_text)
  string(REGEX MATCH
    "build [^\r\n]*gentest_codegen/tu_[0-9]+_[^ ]+\\.gentest\\.h[^\r\n]*: CUSTOM_COMMAND[^\r\n]*[\r\n]+  COMMAND = ([^\r\n]+)"
    _command_block
    "${_build_ninja_text}")
  if(_command_block STREQUAL "")
    message(FATAL_ERROR "Expected '${_build_ninja}' to declare the Gentest codegen command")
  endif()
  set(_command_text "${CMAKE_MATCH_1}")
  if(WIN32)
    _gentest_try_read_windows_launcher("${_command_text}" "${build_dir}" _launcher_text)
    if(NOT _launcher_text STREQUAL "")
      set(_command_text "${_launcher_text}")
    endif()
  endif()
  set(${out_var} "${_command_text}" PARENT_SCOPE)
endfunction()

function(_gentest_assert_required_scanner build_dir expected_scanner)
  _gentest_extract_codegen_command("${build_dir}" _command_text)
  if(_command_text MATCHES "--scan-deps-mode")
    message(FATAL_ERROR "Generated codegen command still exposes the removed --scan-deps-mode option: ${_command_text}")
  endif()
  set(_expected_regex "${expected_scanner}")
  string(REGEX REPLACE "([][+.*^$(){}|\\\\])" "\\\\\\1" _expected_regex "${_expected_regex}")
  if(NOT _command_text MATCHES "--clang-scan-deps(=| )${_expected_regex}")
    message(FATAL_ERROR
      "Expected generated codegen command to require --clang-scan-deps=${expected_scanner}. Command: ${_command_text}")
  endif()
endfunction()

message(STATUS "Configure consumer with automatically selected clang-scan-deps...")
_gentest_configure_consumer("${_consumer_build_dir}")
_gentest_assert_required_scanner("${_consumer_build_dir}" "${_scan_deps}")
file(STRINGS "${_consumer_build_dir}/gentest_codegen_target.txt" _consumer_codegen_target LIMIT_COUNT 1)
if("${_consumer_codegen_target}" STREQUAL "")
  message(FATAL_ERROR "Consumer fixture did not expose its GENTEST_CODEGEN_DEP_TARGET")
endif()

message(STATUS "Build codegen through the required scanner...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
    "${CMAKE_COMMAND}" --build "${_consumer_build_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _codegen_output
  STRIP_TRAILING_WHITESPACE)
string(FIND "${_codegen_output}" "gentest_codegen: info: using clang-scan-deps for named-module dependency discovery" _scan_deps_pos)
if(_scan_deps_pos EQUAL -1)
  message(FATAL_ERROR "Expected actual clang-scan-deps usage. Output:\n${_codegen_output}")
endif()

message(STATUS "Build and run the module consumer...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_dir}" --target gentest_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
set(_consumer_exe "${_consumer_build_dir}/gentest_consumer${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_consumer_exe}" --run=consumer/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_consumer_exe}" --run=consumer/log_sink
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_missing_scan_deps "${_work_dir}/missing-clang-scan-deps")
message(STATUS "Configure consumer with an unusable required scanner...")
_gentest_configure_consumer(
  "${_consumer_missing_build_dir}"
  "-DGENTEST_CODEGEN_CLANG_SCAN_DEPS=${_missing_scan_deps}")
_gentest_assert_required_scanner("${_consumer_missing_build_dir}" "${_missing_scan_deps}")

message(STATUS "Require module codegen to fail instead of source-scanning as a fallback...")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_missing_build_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _missing_rc
  OUTPUT_VARIABLE _missing_out
  ERROR_VARIABLE _missing_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(_missing_rc EQUAL 0)
  message(FATAL_ERROR "Expected module codegen to fail with an unusable required clang-scan-deps executable")
endif()
set(_missing_all "${_missing_out}\n${_missing_err}")
string(FIND "${_missing_all}" "failed to resolve named-module dependencies via clang-scan-deps" _missing_pos)
if(_missing_pos EQUAL -1)
  message(FATAL_ERROR "Expected required clang-scan-deps failure. Output:\n${_missing_all}")
endif()

message(STATUS "Named-module codegen requires clang-scan-deps and exposes no OFF/ON/AUTO policy")
gentest_remove_fixture_path("${_work_dir}")
