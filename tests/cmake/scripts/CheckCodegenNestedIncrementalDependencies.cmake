# Exercise depfiles consumed below the compilation-database root without building
# the runtime: the codegen target includes generation and manifest validation.
foreach(_required IN ITEMS SOURCE_DIR BUILD_ROOT GENTEST_SOURCE_DIR PROG CXX_COMPILER)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckCodegenNestedIncrementalDependencies.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")

gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _ninja)
  message(FATAL_ERROR "Nested incremental dependency regression requires Ninja: ${_ninja_reason}")
endif()

set(_work_dir "${BUILD_ROOT}/nested_incremental_deps")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
  "-DCMAKE_BUILD_TYPE=RelWithDebInfo")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED GENTEST_CODEGEN_HOST_CLANG AND NOT "${GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
  list(APPEND _cache_args "-DGENTEST_CODEGEN_HOST_CLANG=${GENTEST_CODEGEN_HOST_CLANG}")
endif()

function(_gentest_expect_registration expected unexpected)
  file(READ "${_registration_cpp}" _text)
  string(FIND "${_text}" "${expected}" _expected_pos)
  string(FIND "${_text}" "${unexpected}" _unexpected_pos)
  if(_expected_pos EQUAL -1 OR NOT _unexpected_pos EQUAL -1)
    message(FATAL_ERROR
      "${_generator}: expected '${expected}' and no '${unexpected}' in '${_registration_cpp}'.\n${_text}")
  endif()
endfunction()

function(_gentest_expect_noop)
  gentest_check_run_or_fail(
    COMMAND "${_ninja}" ${_ninja_config_args} -v "${CODEGEN_TARGET}"
    WORKING_DIRECTORY "${_build_dir}"
    OUTPUT_VARIABLE _out
    STRIP_TRAILING_WHITESPACE)

  # Output timestamps are insufficient: codegen can run without changing bytes.
  string(STRIP "${_out}" _out)
  if(NOT _out STREQUAL "ninja: no work to do.")
    message(FATAL_ERROR "${_generator}: an unchanged build scheduled work.\n${_out}")
  endif()
endfunction()

foreach(_generator IN ITEMS "Ninja" "Ninja Multi-Config")
  string(MAKE_C_IDENTIFIER "${_generator}" _variant)
  set(_src_dir "${_work_dir}/${_variant}/src")
  set(_build_dir "${_work_dir}/${_variant}/build")
  file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

  message(STATUS "Configure nested incremental dependency fixture with ${_generator}...")
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" -G "${_generator}" -S "${_src_dir}" -B "${_build_dir}" ${_cache_args}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)

  include("${_build_dir}/codegen_paths_RelWithDebInfo.cmake")
  set(_registration_outputs "${CODEGEN_OUTPUTS}")
  list(FILTER _registration_outputs INCLUDE REGEX "\\.header_registration\\.gentest\\.cpp$")
  list(LENGTH _registration_outputs _registration_count)
  if(NOT _registration_count EQUAL 1)
    message(FATAL_ERROR "Expected one generated registration source, got '${_registration_outputs}'")
  endif()
  list(GET _registration_outputs 0 _registration_cpp)
  set(_build_command "${CMAKE_COMMAND}" --build "${_build_dir}" --config RelWithDebInfo --target "${CODEGEN_TARGET}")
  set(_ninja_config_args)
  if(_generator STREQUAL "Ninja Multi-Config")
    set(_ninja_config_args -f build-RelWithDebInfo.ninja)
  endif()

  gentest_check_run_or_fail(
    COMMAND ${_build_command}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  _gentest_expect_registration("nested/header/before" "nested/header/after")
  _gentest_expect_noop()

  # Only the discovered header changes; its literal '$' must survive depfile parsing.
  gentest_fixture_write_file("${_src_dir}/nested/cases$detail.hpp" [=[
#pragma once

[[using gentest: test("nested/header/after")]]
void nested_case();
]=])
  gentest_check_run_or_fail(
    COMMAND ${_build_command}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  _gentest_expect_registration("nested/header/after" "nested/header/before")
  _gentest_expect_noop()
endforeach()

# A direct Ninja rule consumes the depfile without CMake's path transformation.
# Keep the compilation database in a different directory to exercise this base too.
set(_compdb_dir "${_build_dir}")
set(_build_dir "${_work_dir}/native")
file(MAKE_DIRECTORY "${_build_dir}")
set(_driver [=[
execute_process(
  COMMAND "@PROG@"
    --compdb "@_compdb_dir@"
    --tu-out-dir "@_build_dir@"
    --textual-registration-output "@_build_dir@/registration$.cpp"
    --scan-slot-kind authored-tu
    --artifact-manifest "@_build_dir@/manifest$.json"
    --compile-context-id "native:@_src_dir@/nested/cases.cpp"
    --depfile "@_build_dir@/registration.d"
    "@_src_dir@/nested/cases.cpp"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Native Ninja codegen failed: ${_rc}")
endif()
]=])
string(CONFIGURE "${_driver}" _driver @ONLY)
file(WRITE "${_build_dir}/codegen.cmake" "${_driver}")

string(REPLACE "$" "$$" _ninja_cmake "${CMAKE_COMMAND}")
# Codegen preserves unchanged outputs, including the manifest after a case-name edit.
file(WRITE "${_build_dir}/build.ninja"
  "rule codegen\n"
  "  command = \"${_ninja_cmake}\" -P codegen.cmake\n"
  "  depfile = registration.d\n"
  "  restat = 1\n"
  "build registration$$.cpp manifest$$.json: codegen\n")

set(_generator "native Ninja")
set(_ninja_config_args)
set(CODEGEN_TARGET registration$.cpp)
set(_registration_cpp "${_build_dir}/registration$.cpp")
gentest_check_run_or_fail(
  COMMAND "${_ninja}" "${CODEGEN_TARGET}"
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)
_gentest_expect_registration("nested/header/after" "nested/header/before")
_gentest_expect_noop()

# The dependency parser must retain the dollar-sign header as well as the targets.
gentest_fixture_write_file("${_src_dir}/nested/cases$detail.hpp" [=[
#pragma once

[[using gentest: test("nested/header/native")]]
void nested_case();
]=])
gentest_check_run_or_fail(
  COMMAND "${_ninja}" "${CODEGEN_TARGET}"
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)
_gentest_expect_registration("nested/header/native" "nested/header/after")
_gentest_expect_noop()

message(STATUS "Nested incremental dependencies and native Ninja depfile consumption passed")
