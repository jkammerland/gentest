# Requires:
#  -DSOURCE_DIR=<path>
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

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

set(_work_dir "${BUILD_ROOT}/codegen_public_module_imports")
set(_producer_build_dir "${_work_dir}/producer")
set(_consumer_build_off_dir "${_work_dir}/consumer_off")
set(_consumer_build_auto_dir "${_work_dir}/consumer_auto")
set(_consumer_build_on_dir "${_work_dir}/consumer_on")
set(_consumer_build_auto_bad_dir "${_work_dir}/consumer_auto_bad")
set(_consumer_build_on_bad_dir "${_work_dir}/consumer_on_bad")
set(_install_prefix "${_work_dir}/install")
set(_consumer_source_dir "${GENTEST_SOURCE_DIR}/tests/consumer")
set(_consumer_codegen_target gentest_codegen_gentest_consumer)

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
set(_scan_deps_regex "")
if(_scan_deps)
  set(_scan_deps_regex "${_scan_deps}")
  string(REGEX REPLACE "([][+.*^$(){}|\\\\])" "\\\\\\1" _scan_deps_regex "${_scan_deps_regex}")
endif()
set(_common_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  )
if(_scan_deps)
  list(APPEND _common_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_scan_deps}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _common_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _common_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(CMAKE_HOST_WIN32 AND _clangxx MATCHES "[Cc]lang")
  list(APPEND _common_cache_args
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>"
    "-DCMAKE_CXX_FLAGS=-D_ITERATOR_DEBUG_LEVEL=0 -D_HAS_ITERATOR_DEBUGGING=0")
endif()
gentest_append_host_apple_sysroot(_common_cache_args)

message(STATUS "Configure producer for codegen public module imports regression...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=OFF"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build and install producer for codegen public module imports regression...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

function(_gentest_configure_consumer build_dir scan_mode)
  set(_extra_cache_args ${ARGN})
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
      "-DGENTEST_CODEGEN_SCAN_DEPS_MODE=${scan_mode}"
      ${_extra_cache_args}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endfunction()

function(_gentest_assert_codegen_mode build_dir scan_mode)
  set(_expected_scan_deps "${_scan_deps}")
  if(ARGC GREATER 2)
    set(_expected_scan_deps "${ARGV2}")
  endif()
  set(_build_ninja "${build_dir}/build.ninja")
  if(NOT EXISTS "${_build_ninja}")
    message(FATAL_ERROR "Expected build.ninja at '${_build_ninja}'")
  endif()
  file(READ "${_build_ninja}" _build_ninja_text)
  if(NOT _build_ninja_text MATCHES "--scan-deps-mode=${scan_mode}")
    message(FATAL_ERROR
      "Expected build-time gentest_codegen command in '${_build_ninja}' to propagate --scan-deps-mode=${scan_mode}")
  endif()
  if(_expected_scan_deps)
    set(_expected_scan_deps_regex "${_expected_scan_deps}")
    string(REGEX REPLACE "([][+.*^$(){}|\\\\])" "\\\\\\1" _expected_scan_deps_regex "${_expected_scan_deps_regex}")
    if(NOT _build_ninja_text MATCHES "--clang-scan-deps(=| )${_expected_scan_deps_regex}")
      message(FATAL_ERROR
        "Expected build-time gentest_codegen command in '${_build_ninja}' to propagate --clang-scan-deps=${_expected_scan_deps}")
    endif()
  endif()
endfunction()

message(STATUS "Configure consumer with scan-deps disabled...")
_gentest_configure_consumer("${_consumer_build_off_dir}" "OFF")
_gentest_assert_codegen_mode("${_consumer_build_off_dir}" "OFF")

message(STATUS "Build build-time gentest_codegen target with scan-deps disabled...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_off_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build consumer with scan-deps disabled...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_off_dir}" --target gentest_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_consumer_off_exe "${_consumer_build_off_dir}/gentest_consumer${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run consumer smoke with scan-deps disabled...")
gentest_check_run_or_fail(
  COMMAND "${_consumer_off_exe}" --run=consumer/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_missing_scan_deps "${_work_dir}/missing-clang-scan-deps")

message(STATUS "Configure consumer with scan-deps auto mode and missing scanner...")
_gentest_configure_consumer("${_consumer_build_auto_bad_dir}" "AUTO"
  "-DGENTEST_CODEGEN_CLANG_SCAN_DEPS=${_missing_scan_deps}")
_gentest_assert_codegen_mode("${_consumer_build_auto_bad_dir}" "AUTO" "${_missing_scan_deps}")

message(STATUS "Build build-time gentest_codegen target with scan-deps auto mode and missing scanner...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
    "${CMAKE_COMMAND}" --build "${_consumer_build_auto_bad_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _auto_bad_codegen_output
  STRIP_TRAILING_WHITESPACE)

string(FIND "${_auto_bad_codegen_output}" "gentest_codegen: info: falling back to source-scan named-module discovery" _auto_bad_fallback_pos)
if(_auto_bad_fallback_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected build-time gentest_codegen AUTO missing-scanner leg to report source-scan fallback. Output:\n${_auto_bad_codegen_output}")
endif()

message(STATUS "Build consumer with scan-deps auto mode and missing scanner...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_auto_bad_dir}" --target gentest_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_consumer_auto_bad_exe "${_consumer_build_auto_bad_dir}/gentest_consumer${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run consumer smoke with scan-deps auto mode and missing scanner...")
gentest_check_run_or_fail(
  COMMAND "${_consumer_auto_bad_exe}" --run=consumer/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Configure consumer with scan-deps ON mode and missing scanner...")
_gentest_configure_consumer("${_consumer_build_on_bad_dir}" "ON"
  "-DGENTEST_CODEGEN_CLANG_SCAN_DEPS=${_missing_scan_deps}")
_gentest_assert_codegen_mode("${_consumer_build_on_bad_dir}" "ON" "${_missing_scan_deps}")

message(STATUS "Build build-time gentest_codegen target with scan-deps ON mode and missing scanner (expect failure)...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "CMAKE_BUILD_PARALLEL_LEVEL=2"
    "CTEST_PARALLEL_LEVEL=2"
    "${CMAKE_COMMAND}" --build "${_consumer_build_on_bad_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _on_missing_rc
  OUTPUT_VARIABLE _on_missing_out
  ERROR_VARIABLE _on_missing_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_on_missing_rc EQUAL 0)
  message(FATAL_ERROR
    "Expected build-time gentest_codegen target to fail in scan-deps ON mode when clang-scan-deps is unavailable")
endif()

set(_on_missing_all "${_on_missing_out}\n${_on_missing_err}")
string(FIND "${_on_missing_all}" "failed to resolve named-module dependencies via clang-scan-deps (mode=ON)" _on_missing_pos)
if(_on_missing_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected scan-deps ON mode failure message in build output. Output:\n${_on_missing_all}")
endif()

if(NOT _scan_deps)
  message(STATUS "clang-scan-deps not found; covered OFF plus missing-scanner AUTO/ON paths only")
  return()
endif()

message(STATUS "Configure consumer with scan-deps enabled...")
_gentest_configure_consumer("${_consumer_build_on_dir}" "ON")
_gentest_assert_codegen_mode("${_consumer_build_on_dir}" "ON")

message(STATUS "Build build-time gentest_codegen target with scan-deps enabled...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
    "${CMAKE_COMMAND}" --build "${_consumer_build_on_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _on_codegen_output
  STRIP_TRAILING_WHITESPACE)

string(FIND "${_on_codegen_output}" "gentest_codegen: info: using clang-scan-deps for named-module dependency discovery" _on_scan_deps_pos)
if(_on_scan_deps_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected build-time gentest_codegen ON leg to report actual clang-scan-deps usage. Output:\n${_on_codegen_output}")
endif()

message(STATUS "Build consumer with scan-deps enabled...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_on_dir}" --target gentest_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_consumer_exe "${_consumer_build_on_dir}/gentest_consumer${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run consumer smoke with scan-deps enabled...")
gentest_check_run_or_fail(
  COMMAND "${_consumer_exe}" --run=consumer/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Configure consumer with scan-deps auto mode...")
_gentest_configure_consumer("${_consumer_build_auto_dir}" "AUTO")
_gentest_assert_codegen_mode("${_consumer_build_auto_dir}" "AUTO")

message(STATUS "Build build-time gentest_codegen target with scan-deps auto mode...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
    "${CMAKE_COMMAND}" --build "${_consumer_build_auto_dir}" --target "${_consumer_codegen_target}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _auto_codegen_output
  STRIP_TRAILING_WHITESPACE)

string(FIND "${_auto_codegen_output}" "gentest_codegen: info: using clang-scan-deps for named-module dependency discovery" _auto_scan_deps_pos)
if(_auto_scan_deps_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected build-time gentest_codegen AUTO leg to report actual clang-scan-deps usage. Output:\n${_auto_codegen_output}")
endif()

message(STATUS "Build consumer with scan-deps auto mode...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_auto_dir}" --target gentest_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_consumer_auto_exe "${_consumer_build_auto_dir}/gentest_consumer${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run consumer smoke with scan-deps auto mode...")
gentest_check_run_or_fail(
  COMMAND "${_consumer_auto_exe}" --run=consumer/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed public module import consumer success on the real build-time codegen path for scan-deps OFF/ON/AUTO")
