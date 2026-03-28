if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenModuleCacheResourceDirIsolation.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenModuleCacheResourceDirIsolation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenModuleCacheResourceDirIsolation.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if("${_clangxx}" STREQUAL "")
  gentest_skip_test("module cache resource-dir isolation regression: no clang++ compiler available")
  return()
endif()

execute_process(
  COMMAND "${_clangxx}" -print-resource-dir
  RESULT_VARIABLE _resource_dir_rc
  OUTPUT_VARIABLE _resource_dir_out
  ERROR_VARIABLE _resource_dir_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _resource_dir_rc EQUAL 0 OR "${_resource_dir_out}" STREQUAL "")
  gentest_skip_test("module cache resource-dir isolation regression: failed to resolve clang resource dir")
  return()
endif()

set(_resource_dir_primary "${_resource_dir_out}")
set(_resource_dir_alias "${_resource_dir_primary}/.")
if(NOT EXISTS "${_resource_dir_primary}" OR NOT EXISTS "${_resource_dir_alias}")
  gentest_skip_test("module cache resource-dir isolation regression: resource-dir override paths are not usable")
  return()
endif()

file(TO_CMAKE_PATH "${_clangxx}" _clangxx_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_module_cache_resource_dir_isolation")
set(_generated_dir "${_work_dir}/generated")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_generated_dir}")

gentest_fixture_write_file("${_work_dir}/suite.cppm" [=[
export module gentest.cache.resource_dir;
import gentest;

[[using gentest: test("cache/resource_dir")]]
void cache_resource_dir_case() {}
]=])

file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}/suite.cppm" _suite_source_abs)
gentest_fixture_make_compdb_entry(_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "suite.cppm"
  ARGUMENTS "${_clangxx_norm}" "-std=c++20" "-I${_source_dir_norm}/include" "-c" "suite.cppm" "-o" "suite.o")
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_entry}")

function(_gentest_run_codegen_with_resource_dir resource_dir)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "CXX=${_clangxx_norm}"
      "GENTEST_CODEGEN_RESOURCE_DIR=${resource_dir}"
      "${PROG}" --check --compdb "${_work_dir}" --tu-out-dir "${_generated_dir}" "${_suite_source_abs}"
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "module cache resource-dir isolation: gentest_codegen failed unexpectedly for resource dir '${resource_dir}'.\n"
      "Output:\n${_out}\nErrors:\n${_err}")
  endif()
endfunction()

_gentest_run_codegen_with_resource_dir("${_resource_dir_primary}")
_gentest_run_codegen_with_resource_dir("${_resource_dir_alias}")

if(EXISTS "${_generated_dir}/.gentest_codegen_modules")
  message(FATAL_ERROR
    "Expected hashed module cache directories, but found legacy shared cache directory '${_generated_dir}/.gentest_codegen_modules'")
endif()

file(GLOB _module_cache_dirs LIST_DIRECTORIES TRUE "${_generated_dir}/.gentest_codegen_modules_*")
list(SORT _module_cache_dirs)
list(LENGTH _module_cache_dirs _module_cache_dir_count)
if(NOT _module_cache_dir_count EQUAL 2)
  message(FATAL_ERROR
    "module cache resource-dir isolation: expected 2 distinct cache directories after changing the resource dir override, found '${_module_cache_dir_count}' in '${_generated_dir}'")
endif()

