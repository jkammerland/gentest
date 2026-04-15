# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<path>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMultilineDirectiveFailure.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMultilineDirectiveFailure.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMultilineDirectiveFailure.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_multiline_directives")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module multiline directive regression: clang/clang++ not found")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("module multiline directive regression: ${_supported_ninja_reason}")
    return()
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _configure_rc
  OUTPUT_VARIABLE _configure_out
  ERROR_VARIABLE _configure_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _configure_rc EQUAL 0)
  message(FATAL_ERROR "Configure failed unexpectedly.\n${_configure_out}\n${_configure_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target multiline_directive_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Multiline module directive regression failed to build.\n${_build_out}\n${_build_err}")
endif()

set(_exe "${_build_dir}/multiline_directive_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()

execute_process(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR "Listing multiline directive fixture failed.\n${_list_out}\n${_list_err}")
endif()
string(FIND "${_list_out}" "multiline/basic" _basic_pos)
if(_basic_pos EQUAL -1)
  string(FIND "${_list_out}" "multiline/module_mock" _basic_pos)
endif()
if(_basic_pos EQUAL -1)
  message(FATAL_ERROR "Expected multiline/module_mock in --list-tests output.\n${_list_out}")
endif()

execute_process(
  COMMAND "${_exe}" --run=multiline/module_mock
  WORKING_DIRECTORY "${_build_dir}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE _run_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "Running multiline/module_mock failed.\n${_run_out}\n${_run_err}")
endif()

message(STATUS "Multiline module directive regression passed")
