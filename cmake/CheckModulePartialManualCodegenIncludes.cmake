# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenIncludes.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenIncludes.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenIncludes.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenIncludes.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_partial_manual_codegen_includes")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("partial manual module codegen include regression: clang/clang++ not found")
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
    gentest_skip_test("partial manual module codegen include regression: ${_supported_ninja_reason}")
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
gentest_append_host_apple_sysroot(_cmake_cache_args)

gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target partial_manual_codegen_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB _wrapper_candidates "${_build_dir}/generated/*.module.gentest.cppm")
list(LENGTH _wrapper_candidates _wrapper_count)
if(NOT _wrapper_count EQUAL 2)
  message(FATAL_ERROR "Expected exactly two generated module wrappers, found ${_wrapper_count}: ${_wrapper_candidates}")
endif()

set(_registry_wrapper "")
set(_impl_wrapper "")
foreach(_candidate IN LISTS _wrapper_candidates)
  file(READ "${_candidate}" _candidate_text)
  string(FIND "${_candidate_text}" "export module gentest.partial_manual_registry;" _registry_pos)
  if(NOT _registry_pos EQUAL -1)
    set(_registry_wrapper "${_candidate}")
    set(_registry_text "${_candidate_text}")
  endif()
  string(FIND "${_candidate_text}" "export module gentest.partial_manual_impl;" _impl_pos)
  if(NOT _impl_pos EQUAL -1)
    set(_impl_wrapper "${_candidate}")
    set(_impl_text "${_candidate_text}")
  endif()
endforeach()

if("${_registry_wrapper}" STREQUAL "" OR "${_impl_wrapper}" STREQUAL "")
  message(FATAL_ERROR "Expected registry-only and impl-only wrappers, found:\n${_wrapper_candidates}")
endif()

string(FIND "${_registry_text}" "gentest/mock_registry_codegen.h" _registry_include_pos)
string(FIND "${_registry_text}" "gentest/mock_codegen.h" _registry_combined_pos)
if(_registry_include_pos EQUAL -1 OR NOT _registry_combined_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected registry-only wrapper to preserve gentest/mock_registry_codegen.h without injecting gentest/mock_codegen.h.\n"
    "Wrapper: ${_registry_wrapper}\n${_registry_text}")
endif()

string(FIND "${_impl_text}" "gentest/mock_impl_codegen.h" _impl_include_pos)
string(FIND "${_impl_text}" "gentest/mock_codegen.h" _impl_combined_pos)
if(_impl_include_pos EQUAL -1 OR NOT _impl_combined_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected impl-only wrapper to preserve gentest/mock_impl_codegen.h without injecting gentest/mock_codegen.h.\n"
    "Wrapper: ${_impl_wrapper}\n${_impl_text}")
endif()

set(_exe "${_build_dir}/partial_manual_codegen_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_out)

foreach(_expected IN ITEMS
    "partial_manual/registry_only"
    "partial_manual/impl_only")
  string(FIND "${_list_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "Expected partial manual include test was missing from --list-tests output: '${_expected}'.\n${_list_out}")
  endif()
endforeach()

message(STATUS "Module partial manual codegen include regression passed")
