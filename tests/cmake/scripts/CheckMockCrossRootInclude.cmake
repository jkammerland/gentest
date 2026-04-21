# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<build tree root>
#  -DPROJECT_SOURCE_DIR=<project source root> (or GENTEST_SOURCE_DIR/SOURCE_DIR)
#  -DCODEGEN_STD=<std flag, e.g. -std=c++23>
# Optional:
#  -DCROSS_TARGET_ARG=<--target=...>
#
# Behavior:
#  - On Windows, if BUILD_ROOT and USERPROFILE are on different drives, this
#    script generates a mock target header on USERPROFILE drive and runs codegen
#    emit mode with outputs in BUILD_ROOT. This exercises cross-root include
#    path rendering in render_mocks().
#  - If both roots are the same, script reports a skip and succeeds.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  if(DEFINED PROJECT_SOURCE_DIR AND NOT "${PROJECT_SOURCE_DIR}" STREQUAL "")
    set(SOURCE_DIR "${PROJECT_SOURCE_DIR}")
  elseif(DEFINED GENTEST_SOURCE_DIR AND NOT "${GENTEST_SOURCE_DIR}" STREQUAL "")
    set(SOURCE_DIR "${GENTEST_SOURCE_DIR}")
  else()
    message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: SOURCE_DIR (or PROJECT_SOURCE_DIR/GENTEST_SOURCE_DIR) not set")
  endif()
endif()
if(NOT DEFINED TESTS_SOURCE_DIR OR "${TESTS_SOURCE_DIR}" STREQUAL "")
  set(TESTS_SOURCE_DIR "${SOURCE_DIR}/tests")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: CODEGEN_STD not set")
endif()
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
file(TO_CMAKE_PATH "${TESTS_SOURCE_DIR}" _tests_source_dir_norm)

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
set(_mock_cross_root_fixture_dir "${_source_dir_norm}/tests/cmake/mock_cross_root_include")

if(NOT WIN32)
  gentest_skip_test("CheckMockCrossRootInclude.cmake: non-Windows host")
  return()
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("CheckMockCrossRootInclude.cmake: clang/clang++ not found")
  return()
endif()

execute_process(
  COMMAND "${_clangxx}" -print-resource-dir
  RESULT_VARIABLE _resource_dir_rc
  OUTPUT_VARIABLE _resource_dir
  ERROR_VARIABLE _resource_dir_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _resource_dir_rc EQUAL 0 OR "${_resource_dir}" STREQUAL "")
  gentest_skip_test("CheckMockCrossRootInclude.cmake: failed to query clang resource dir from '${_clangxx}': ${_resource_dir_err}")
  return()
endif()

set(_user_profile "$ENV{USERPROFILE}")
if(_user_profile STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: USERPROFILE is empty")
endif()

file(TO_CMAKE_PATH "${BUILD_ROOT}" _build_root)
file(TO_CMAKE_PATH "${_user_profile}" _user_profile_norm)

string(REGEX MATCH "^[A-Za-z]:" _build_drive "${_build_root}")
string(REGEX MATCH "^[A-Za-z]:" _user_drive "${_user_profile_norm}")

if(_build_drive STREQUAL "" OR _user_drive STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: unable to determine drive roots (build='${_build_root}', user='${_user_profile_norm}')")
endif()

if(_build_drive STREQUAL _user_drive)
  gentest_skip_test("CheckMockCrossRootInclude.cmake: build/user on same drive (${_build_drive}); skipping cross-root assertion")
  return()
endif()

string(RANDOM LENGTH 8 _cross_root_suffix)
set(_external_dir "${_user_profile_norm}/gentest_codegen_cross_root/${_cross_root_suffix}")
unset(_cross_root_suffix)
set(_external_header "${_external_dir}/cross_root_sink.hpp")
file(MAKE_DIRECTORY "${_external_dir}")
file(COPY "${_mock_cross_root_fixture_dir}/cross_root_sink.hpp"
     DESTINATION "${_external_dir}")

set(_work_dir "${_build_root}/gentest_codegen_cross_root")
file(MAKE_DIRECTORY "${_work_dir}")
set(_input_cpp "${_work_dir}/cross_root_input.cpp")
set(_output_header "${_work_dir}/cross_root_output.gentest.h")
set(_mock_registry "${_work_dir}/cross_root_mock_registry.hpp")
set(_mock_impl "${_work_dir}/cross_root_mock_impl.hpp")
set(_mock_registry_domain "${_work_dir}/cross_root_mock_registry__domain_0000_header.hpp")
set(_mock_impl_domain "${_work_dir}/cross_root_mock_impl__domain_0000_header.hpp")
macro(_gentest_check_cross_root_fail message)
  file(REMOVE_RECURSE "${_external_dir}")
  message(FATAL_ERROR "${message}")
endmacro()

file(TO_CMAKE_PATH "${_external_header}" _external_header_norm)
set(CROSS_ROOT_HEADER "${_external_header_norm}")
configure_file("${_mock_cross_root_fixture_dir}/cross_root_input.cpp.in" "${_input_cpp}" @ONLY)
unset(CROSS_ROOT_HEADER)

set(_args
  --discover-mocks
  --tu-out-dir "${_work_dir}"
  --tu-header-output "${_output_header}"
  --mock-registry "${_mock_registry}"
  --mock-impl "${_mock_impl}"
  --mock-domain-registry-output "${_mock_registry_domain}"
  --mock-domain-impl-output "${_mock_impl_domain}"
  "${_input_cpp}"
  --)

if(DEFINED CROSS_TARGET_ARG AND NOT "${CROSS_TARGET_ARG}" STREQUAL "")
  list(APPEND _args "${CROSS_TARGET_ARG}")
endif()

set(_codegen_std "${CODEGEN_STD}")
if(_codegen_std MATCHES "^/std:c\\+\\+([0-9]+)$")
  set(_codegen_std "-std=c++${CMAKE_MATCH_1}")
elseif(_codegen_std STREQUAL "/std:c++latest")
  # clang does not understand MSVC's /std:c++latest spelling.
  set(_codegen_std "-std=c++23")
endif()

list(APPEND _args
  -x
  c++
  "${_codegen_std}"
  "-I${_source_dir_norm}/include"
  "-I${_tests_source_dir_norm}")

if(EXISTS "${_build_root}/_deps/fmt-src/include")
  list(APPEND _args "-I${_build_root}/_deps/fmt-src/include")
endif()
file(GLOB _vcpkg_include_dirs LIST_DIRECTORIES true "${_build_root}/vcpkg_installed/*/include")
foreach(_inc IN LISTS _vcpkg_include_dirs)
  if(IS_DIRECTORY "${_inc}")
    list(APPEND _args "-I${_inc}")
  endif()
endforeach()
unset(_inc)
unset(_vcpkg_include_dirs)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "CC=${_clang}"
          "CXX=${_clangxx}"
          "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}"
          "${PROG}" ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  _gentest_check_cross_root_fail("Cross-root mock codegen failed (rc=${_rc}). Output:\n${_out}\nErrors:\n${_err}")
endif()

if(NOT EXISTS "${_mock_registry}")
  _gentest_check_cross_root_fail("Cross-root mock codegen did not produce registry header: ${_mock_registry}")
endif()
if(NOT EXISTS "${_mock_registry_domain}")
  _gentest_check_cross_root_fail("Cross-root mock codegen did not produce domain registry header: ${_mock_registry_domain}")
endif()

file(READ "${_mock_registry_domain}" _registry_text)
set(_expected_include "#include \"${_external_header_norm}\"")
string(TOLOWER "${_registry_text}" _registry_text_cmp)
string(TOLOWER "${_expected_include}" _expected_include_cmp)
string(FIND "${_registry_text_cmp}" "${_expected_include_cmp}" _include_pos)
if(_include_pos EQUAL -1)
  _gentest_check_cross_root_fail(
    "Expected absolute include not found in domain registry header. Wanted '${_expected_include}'.\n${_registry_text}")
endif()

file(REMOVE_RECURSE "${_external_dir}")
message(STATUS "Cross-root mock include generation passed")
