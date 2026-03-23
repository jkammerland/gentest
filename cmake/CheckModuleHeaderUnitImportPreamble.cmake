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
  message(FATAL_ERROR "CheckModuleHeaderUnitImportPreamble.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleHeaderUnitImportPreamble.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleHeaderUnitImportPreamble.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_header_unit_import_preamble")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module header-unit import preamble regression: clang/clang++ not found")
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
    gentest_skip_test("module header-unit import preamble regression: ${_supported_ninja_reason}")
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
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_unit_import_preamble_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
set(_header_unit_unavailable FALSE)
if(NOT _build_rc EQUAL 0)
  if(_build_out MATCHES "cannot be imported because it is not known to be a header unit"
     OR _build_err MATCHES "cannot be imported because it is not known to be a header unit")
    set(_header_unit_unavailable TRUE)
  else()
    message(FATAL_ERROR "Build failed unexpectedly.\n${_build_out}\n${_build_err}")
  endif()
endif()

if(_header_unit_unavailable)
  message(STATUS "Header-unit import build is unavailable on this toolchain; skipping explicit mock consumer run")
  return()
endif()

set(_mock_surface "${_build_dir}/generated/mocks/header_unit_import_preamble_mocks.cppm")
if(NOT EXISTS "${_mock_surface}")
  message(FATAL_ERROR "Expected explicit mock module surface was not written: ${_mock_surface}")
endif()

file(GLOB _wrapper_candidates "${_build_dir}/generated/*.module.gentest.cppm")
list(LENGTH _wrapper_candidates _wrapper_count)
if(NOT _wrapper_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one generated module wrapper, found ${_wrapper_count}: ${_wrapper_candidates}\n${_build_out}\n${_build_err}")
endif()
list(GET _wrapper_candidates 0 _wrapper)
file(READ "${_wrapper}" _wrapper_text)

string(FIND "${_wrapper_text}" "import <vector>;" _header_unit_import_pos)

if(_header_unit_import_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected generated wrapper to contain the header-unit import preamble.\n${_wrapper_text}\n${_build_out}\n${_build_err}")
endif()

set(_exe_dir "${_build_dir}")
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  if(EXISTS "${_build_dir}/${BUILD_TYPE}/header_unit_import_preamble_tests${CMAKE_EXECUTABLE_SUFFIX}")
    set(_exe_dir "${_build_dir}/${BUILD_TYPE}")
  endif()
endif()
set(_exe "${_exe_dir}/header_unit_import_preamble_tests${CMAKE_EXECUTABLE_SUFFIX}")

execute_process(
  COMMAND "${_exe}" --run=header_unit/import_preamble
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE _run_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "Running explicit header-unit import preamble case failed unexpectedly.\n${_run_out}\n${_run_err}")
endif()

message(STATUS "Module header-unit import preamble regression passed")
