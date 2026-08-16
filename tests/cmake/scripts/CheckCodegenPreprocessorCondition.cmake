# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DSOURCE_DIR=<gentest source tree>
#  -DSOURCE_FILE=<module fixture to scan>
#  -DCODEGEN_STD=<C++ standard flag>
# The output assertions are the same as CheckNoSubstring.cmake.

foreach(_required_var IN ITEMS PROG BUILD_ROOT SOURCE_DIR SOURCE_FILE CODEGEN_STD)
  if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
    message(FATAL_ERROR "CheckCodegenPreprocessorCondition.cmake: ${_required_var} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("preprocessor-condition module scan regression: clang/clang++ not found")
  return()
endif()
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT _clang_scan_deps)
  gentest_skip_test("preprocessor-condition module scan regression: clang-scan-deps not found")
  return()
endif()

string(SHA256 _source_hash "${SOURCE_FILE}")
string(SUBSTRING "${_source_hash}" 0 12 _source_hash)
set(_work_dir "${BUILD_ROOT}/preprocessor_condition_${_source_hash}")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

gentest_make_public_api_compile_args(
  _compile_args
  COMPILER "${_clangxx}"
  STD "${CODEGEN_STD}"
  SOURCE_ROOT "${SOURCE_DIR}"
  INCLUDE_TESTS
  APPLE_SYSROOT)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _compile_args "${TARGET_ARG}")
endif()
gentest_fixture_make_compdb_entry(
  _compdb_entry
  DIRECTORY "${_work_dir}"
  FILE "${SOURCE_FILE}"
  ARGUMENTS
    ${_compile_args}
    -c "${SOURCE_FILE}"
    -o "${_work_dir}/fixture.o")
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_compdb_entry}")

set(ARGS
  --check
  --compdb "${_work_dir}"
  --clang-scan-deps "${_clang_scan_deps}"
  --host-clang "${_clangxx}"
  "${SOURCE_FILE}")
include("${CMAKE_CURRENT_LIST_DIR}/CheckNoSubstring.cmake")
