# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

function(_gentest_find_mock_domain_artifact out_var generated_dir stem kind expected_text)
  file(GLOB _candidates "${generated_dir}/${stem}__domain_*")
  set(_matches "")
  foreach(_candidate IN LISTS _candidates)
    get_filename_component(_candidate_name "${_candidate}" NAME)
    if(NOT _candidate_name MATCHES "^${stem}__domain_[0-9]+_.*\\.${kind}$")
      continue()
    endif()
    file(READ "${_candidate}" _candidate_text)
    string(FIND "${_candidate_text}" "${expected_text}" _candidate_pos)
    if(NOT _candidate_pos EQUAL -1)
      list(APPEND _matches "${_candidate}")
    endif()
  endforeach()

  list(LENGTH _matches _match_count)
  if(NOT _match_count EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one generated ${stem} domain artifact containing '${expected_text}', found ${_match_count}.\n"
      "Candidates:\n${_candidates}\n"
      "Matches:\n${_matches}")
  endif()

  list(GET _matches 0 _match)
  set(${out_var} "${_match}" PARENT_SCOPE)
endfunction()

set(_work_dir "${BUILD_ROOT}/mixed_module_mock_registry")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)
if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("mixed explicit mock registry regression: no usable C/C++ compiler pair was provided")
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
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("mixed explicit mock registry regression: ${_supported_ninja_reason}")
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
gentest_find_clang_scan_deps(_clang_scan_deps "${_effective_cxx_compiler}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_host_apple_sysroot(_cmake_cache_args)

message(STATUS "Configure mixed explicit mock registry fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build mixed explicit mock registry fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target mixed_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
set(_header_generated_dir "${_generated_dir}/header_mocks")
set(_module_generated_dir "${_generated_dir}/module_mocks")
set(_header_surface "${_header_generated_dir}/public/mixed_legacy_mocks.hpp")
set(_module_surface "${_module_generated_dir}/gentest/mixed_module_mocks.cppm")

foreach(_generated_file IN ITEMS
    "${_header_surface}"
    "${_module_surface}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Expected explicit mock surface was not written: ${_generated_file}")
  endif()
endforeach()

_gentest_find_mock_domain_artifact(_header_registry "${_header_generated_dir}" "mixed_header_mocks_mock_registry" "hpp" "legacy::Service")
_gentest_find_mock_domain_artifact(_module_registry "${_module_generated_dir}" "mixed_module_mocks_mock_registry" "hpp" "mixmod::Service")
_gentest_find_mock_domain_artifact(_extra_registry "${_module_generated_dir}" "mixed_module_mocks_mock_registry" "hpp" "extramod::Worker")
_gentest_find_mock_domain_artifact(_same_block_registry "${_module_generated_dir}" "mixed_module_mocks_mock_registry" "hpp" "sameblock::Service")

set(_prog "${_build_dir}/mixed_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_prog}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed mixed explicit header and module mock registry success")
