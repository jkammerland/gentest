# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${GENTEST_SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("module registration mock split regression: MODULE_REGISTRATION requires a single-config Ninja fixture")
  return()
endif()

gentest_make_compact_fixture_work_dir(_work_dir
  PREFIX mmrs
  SOURCE_DIR "${SOURCE_DIR}"
  EXTRA_KEY "module_registration_mock_split")
set(_src_dir "${_work_dir}/s")
set(_build_dir "${_work_dir}/b")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")
gentest_make_compact_fixture_source_link(_gentest_source_dir
  WORK_DIR "${_work_dir}"
  SOURCE_DIR "${GENTEST_SOURCE_DIR}"
  STEM "g")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)
if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("module registration mock split regression: no usable C/C++ compiler pair was provided")
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
  "-DGENTEST_SOURCE_DIR=${_gentest_source_dir}"
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("module registration mock split regression: ${_supported_ninja_reason}")
  return()
endif()
list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
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
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_append_windows_path_budget_cache_args(_cmake_cache_args)

message(STATUS "Configure module registration mock split fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build module registration mock split fixture (expected failure)...")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_mock_split_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(_build_rc EQUAL 0)
  message(FATAL_ERROR "Expected direct module-owned mock registration to fail")
endif()
set(_build_combined "${_build_out}\n${_build_err}")
set(_expected_error "MODULE_REGISTRATION cannot generate direct mocks owned by named module 'gentest.story035.mock_split_provider'")
string(FIND "${_build_combined}" "${_expected_error}" _expected_error_pos)
if(_expected_error_pos EQUAL -1)
  message(FATAL_ERROR "Expected focused module-owned mock diagnostic '${_expected_error}'.\n${_build_combined}")
endif()

gentest_cleanup_compact_fixture_source_link("${_gentest_source_dir}")
message(STATUS "Observed focused module-owned mock rejection")
