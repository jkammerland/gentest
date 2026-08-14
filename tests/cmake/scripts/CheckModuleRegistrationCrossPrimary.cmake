if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationCrossPrimary.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationCrossPrimary.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationCrossPrimary.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${GENTEST_SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("cross-primary module registration regression: single-config Ninja is required")
  return()
endif()

gentest_make_compact_fixture_work_dir(_work_dir
  PREFIX mrcp
  SOURCE_DIR "${SOURCE_DIR}"
  EXTRA_KEY "module_registration_cross_primary")
set(_src_dir "${_work_dir}/s")
set(_build_dir "${_work_dir}/b")
set(_negative_build_dir "${_work_dir}/n")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")
gentest_make_compact_fixture_source_link(_gentest_source_dir
  WORK_DIR "${_work_dir}"
  SOURCE_DIR "${GENTEST_SOURCE_DIR}"
  STEM "g")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)
if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("cross-primary module registration regression: no usable C/C++ compiler pair was provided")
  return()
endif()
gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("cross-primary module registration regression: ${_supported_ninja_reason}")
  return()
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${_gentest_source_dir}"
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}"
  "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
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

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_build_dir}" ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_cross_primary_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/module_registration_cross_primary_tests")
if(CMAKE_HOST_WIN32)
  string(APPEND _exe ".exe")
endif()
gentest_check_run_or_fail(
  COMMAND "${_exe}" "--filter=story116/consumer/cross_primary/*"
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_negative_build_dir}" ${_cmake_cache_args}
    -DSTORY116_NON_REEXPORT=ON
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_negative_build_dir}" --target module_registration_cross_primary_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _negative_rc
  OUTPUT_VARIABLE _negative_out
  ERROR_VARIABLE _negative_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(_negative_rc EQUAL 0)
  message(FATAL_ERROR "Expected a non-re-exported foreign fixture type to fail importer registration")
endif()
set(_negative_combined "${_negative_out}\n${_negative_err}")
set(_expected "fixture type 'story116::SharedState' is not reachable after importing named module 'gentest.story116.fixture_consumer'")
string(FIND "${_negative_combined}" "${_expected}" _expected_pos)
if(_expected_pos EQUAL -1)
  message(FATAL_ERROR "Expected focused non-re-export diagnostic '${_expected}'.\n${_negative_combined}")
endif()

gentest_cleanup_compact_fixture_source_link("${_gentest_source_dir}")
message(STATUS "Cross-primary module registration regression passed")
