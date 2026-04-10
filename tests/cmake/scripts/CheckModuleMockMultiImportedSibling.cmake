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
  message(FATAL_ERROR "CheckModuleMockMultiImportedSibling.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockMultiImportedSibling.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockMultiImportedSibling.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/mmmis")
else()
  set(_work_dir "${BUILD_ROOT}/module_mock_multi_imported_sibling")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("multi imported sibling module mock regression: no usable clang/clang++ pair was provided")
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
    gentest_skip_test("multi imported sibling module mock regression: ${_supported_ninja_reason}")
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

message(STATUS "Configure multi imported sibling module mock fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build multi imported sibling module mock fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target multi_imported_sibling_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB _mock_provider_wrappers "${_build_dir}/generated/mocks/*.module.gentest.*")
set(_alpha_wrapper_found FALSE)
set(_beta_wrapper_found FALSE)
foreach(_candidate IN LISTS _mock_provider_wrappers)
  file(READ "${_candidate}" _candidate_text)
  string(FIND "${_candidate_text}" "module gentest.multi_imported_sibling_provider_alpha;" _alpha_module_pos)
  if(NOT _alpha_module_pos EQUAL -1)
    set(_alpha_wrapper_found TRUE)
  endif()
  string(FIND "${_candidate_text}" "module gentest.multi_imported_sibling_provider_beta;" _beta_module_pos)
  if(NOT _beta_module_pos EQUAL -1)
    set(_beta_wrapper_found TRUE)
  endif()
  string(FIND "${_candidate_text}" "import gentest.mock;" _mock_import_pos)
  if(_mock_import_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated provider wrapper '${_candidate}' to import gentest.mock.\n"
      "${_candidate_text}")
  endif()
  string(FIND "${_candidate_text}" "#include \"gentest/mock.h\"" _mock_header_include_pos)
  if(NOT _mock_header_include_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated provider wrapper '${_candidate}' to avoid textually including gentest/mock.h.\n"
      "${_candidate_text}")
  endif()
endforeach()
if(NOT _alpha_wrapper_found OR NOT _beta_wrapper_found)
  message(FATAL_ERROR
    "Expected explicit multi-imported-sibling mocks target to generate provider wrappers under '${_build_dir}/generated/mocks'.\n"
    "Alpha found: ${_alpha_wrapper_found}\n"
    "Beta found: ${_beta_wrapper_found}\n"
    "Candidates: ${_mock_provider_wrappers}")
endif()

set(_aggregate_module "${_build_dir}/generated/mocks/gentest/multi_imported_sibling_mocks.cppm")
if(NOT EXISTS "${_aggregate_module}")
  message(FATAL_ERROR "Expected explicit multi-imported-sibling aggregate module was not written: ${_aggregate_module}")
endif()

set(_prog "${_build_dir}/multi_imported_sibling_tests${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run module importer acceptance case with two imported module mocks...")
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=multi_imported_sibling/module_two_module_mocks
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
