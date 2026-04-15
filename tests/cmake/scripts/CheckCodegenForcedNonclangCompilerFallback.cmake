if(WIN32)
  message(STATUS "CheckCodegenForcedNonclangCompilerFallback.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenForcedNonclangCompilerFallback.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenForcedNonclangCompilerFallback.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenForcedNonclangCompilerFallback.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if("${_clangxx}" STREQUAL "")
  gentest_skip_test("forced non-clang compiler fallback regression: no clang++ compiler available")
  return()
endif()

file(TO_CMAKE_PATH "${_clangxx}" _clangxx_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_forced_nonclang_compiler_fallback")
set(_generated_dir "${_work_dir}/generated")
set(_bin_dir "${_work_dir}/bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_generated_dir}" "${_bin_dir}")

gentest_fixture_write_file("${_work_dir}/suite.cppm" [=[
export module gentest.retarget.forced_nonclang;
import gentest;

[[using gentest: test("retarget/forced_nonclang")]]
void forced_nonclang_case() {}
]=])

gentest_fixture_write_file("${_bin_dir}/g++" [=[
#!/bin/sh
echo "forced-gxx-should-not-run $*" >&2
exit 23
]=])
file(CHMOD "${_bin_dir}/g++"
  PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}/suite.cppm" _suite_source_abs)
file(TO_CMAKE_PATH "${_generated_dir}/tu_0000_suite.module.gentest.cppm" _wrapper_abs)
file(TO_CMAKE_PATH "${_bin_dir}/g++" _fake_gxx_abs)
gentest_make_public_api_compile_args(
  _suite_compile_args
  COMPILER "${_fake_gxx_abs}"
  STD "-std=c++20"
  SOURCE_ROOT "${_source_dir_norm}"
  EXTRA_ARGS
    "-c"
    "suite.cppm"
    "-o"
    "suite.o")
gentest_make_public_api_compile_args(
  _wrapper_compile_args
  COMPILER "${_clangxx_norm}"
  STD "-std=c++20"
  SOURCE_ROOT "${_source_dir_norm}"
  EXTRA_ARGS
    "-c"
    "${_wrapper_abs}"
    "-o"
    "${_wrapper_abs}.o")

gentest_fixture_make_compdb_entry(_source_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "suite.cppm"
  ARGUMENTS ${_suite_compile_args})

gentest_fixture_make_compdb_entry(_wrapper_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_wrapper_abs}"
  ARGUMENTS ${_wrapper_compile_args})

gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_source_entry}" "${_wrapper_entry}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "CXX=${_clangxx_norm}"
    "${PROG}" --check --compdb "${_work_dir}" --tu-out-dir "${_generated_dir}"
    --module-wrapper-output "${_wrapper_abs}"
    "${_suite_source_abs}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "forced non-clang compiler fallback: gentest_codegen failed unexpectedly.\n"
    "Output:\n${_out}\nErrors:\n${_err}")
endif()

if(_err MATCHES "forced-gxx-should-not-run")
  message(FATAL_ERROR
    "forced non-clang compiler fallback: gentest_codegen still tried to execute the non-clang compiler.\n"
    "Output:\n${_out}\nErrors:\n${_err}")
endif()
