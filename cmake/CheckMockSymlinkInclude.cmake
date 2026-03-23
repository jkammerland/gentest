# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<build tree root>
#  -DPROJECT_SOURCE_DIR=<project source root>
#  -DCODEGEN_STD=<std flag, e.g. -std=c++23>
# Optional:
#  -DCROSS_TARGET_ARG=<--target=...>
#
# Behavior:
#  - On non-Windows hosts, this script creates a real source tree plus a
#    symlinked view of that tree and verifies generated mock registry headers
#    keep the symlink-visible include path instead of canonicalizing to the
#    real host path.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROJECT_SOURCE_DIR OR "${PROJECT_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: PROJECT_SOURCE_DIR is empty")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
set(_mock_symlink_fixture_dir "${PROJECT_SOURCE_DIR}/tests/cmake/mock_symlink_include")

if(WIN32)
  gentest_skip_test("CheckMockSymlinkInclude.cmake: Windows host covered by cross-root mock include test")
  return()
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("CheckMockSymlinkInclude.cmake: clang/clang++ not found")
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
  gentest_skip_test("CheckMockSymlinkInclude.cmake: failed to query clang resource dir from '${_clangxx}': ${_resource_dir_err}")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/gentest_codegen_symlink_include")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_real_root "${_work_dir}/real")
set(_view_root "${_work_dir}/view")
set(_real_include_dir "${_real_root}/include")
set(_real_src_dir "${_real_root}/src")
file(MAKE_DIRECTORY "${_real_include_dir}" "${_real_src_dir}")

set(_real_header "${_real_include_dir}/symlink_sink.hpp")
set(_input_cpp "${_real_src_dir}/input.cpp")
set(_output_cpp "${_work_dir}/symlink_output.gentest.cpp")
set(_mock_registry "${_work_dir}/symlink_mock_registry.hpp")
set(_mock_impl "${_work_dir}/symlink_mock_impl.hpp")
set(_mock_registry_domain "${_work_dir}/symlink_mock_registry__domain_0000_header.hpp")

file(COPY
  "${_mock_symlink_fixture_dir}/include/symlink_sink.hpp"
  DESTINATION "${_real_include_dir}")
file(COPY
  "${_mock_symlink_fixture_dir}/input.cpp"
  DESTINATION "${_real_src_dir}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_real_root}" "${_view_root}"
  RESULT_VARIABLE _symlink_rc
  OUTPUT_VARIABLE _symlink_out
  ERROR_VARIABLE _symlink_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _symlink_rc EQUAL 0)
  message(FATAL_ERROR "Failed to create symlinked source tree (rc=${_symlink_rc}). Output:\n${_symlink_out}\nErrors:\n${_symlink_err}")
endif()

set(_args
  --discover-mocks
  --output "${_output_cpp}"
  --mock-registry "${_mock_registry}"
  --mock-impl "${_mock_impl}"
  "${_view_root}/src/input.cpp"
  --)

if(DEFINED CROSS_TARGET_ARG AND NOT "${CROSS_TARGET_ARG}" STREQUAL "")
  list(APPEND _args "${CROSS_TARGET_ARG}")
endif()

set(_codegen_std "${CODEGEN_STD}")
if(_codegen_std MATCHES "^/std:c\\+\\+([0-9]+)$")
  set(_codegen_std "-std=c++${CMAKE_MATCH_1}")
elseif(_codegen_std STREQUAL "/std:c++latest")
  set(_codegen_std "-std=c++23")
endif()

list(APPEND _args
  -x
  c++
  "${_codegen_std}"
  "-I${PROJECT_SOURCE_DIR}/include"
  "-I${_view_root}/include")

if(EXISTS "${BUILD_ROOT}/_deps/fmt-src/include")
  list(APPEND _args "-I${BUILD_ROOT}/_deps/fmt-src/include")
endif()
file(GLOB _vcpkg_include_dirs LIST_DIRECTORIES true "${BUILD_ROOT}/vcpkg_installed/*/include")
foreach(_inc IN LISTS _vcpkg_include_dirs)
  if(IS_DIRECTORY "${_inc}")
    list(APPEND _args "-I${_inc}")
  endif()
endforeach()
unset(_inc)
unset(_vcpkg_include_dirs)

set(_cache_file "${BUILD_ROOT}/CMakeCache.txt")
if(EXISTS "${_cache_file}")
  file(STRINGS "${_cache_file}" _fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
  if(_fmt_dir_line)
    list(GET _fmt_dir_line 0 _fmt_dir_value)
    string(REGEX REPLACE "^fmt_DIR:PATH=" "" _fmt_dir "${_fmt_dir_value}")
    get_filename_component(_fmt_prefix "${_fmt_dir}/../../.." ABSOLUTE)
    if(IS_DIRECTORY "${_fmt_prefix}/include")
      list(APPEND _args "-I${_fmt_prefix}/include")
    endif()
    unset(_fmt_dir)
    unset(_fmt_dir_value)
  endif()
  unset(_fmt_dir_line)
endif()
unset(_cache_file)

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
  message(FATAL_ERROR "Symlink mock codegen failed (rc=${_rc}). Output:\n${_out}\nErrors:\n${_err}")
endif()

if(NOT EXISTS "${_mock_registry}")
  message(FATAL_ERROR "Symlink mock codegen did not produce registry header: ${_mock_registry}")
endif()
if(NOT EXISTS "${_mock_registry_domain}")
  message(FATAL_ERROR "Symlink mock codegen did not produce domain registry header: ${_mock_registry_domain}")
endif()

file(READ "${_mock_registry_domain}" _registry_text)
set(_expected_include "#include \"view/include/symlink_sink.hpp\"")
set(_forbidden_include "#include \"real/include/symlink_sink.hpp\"")
string(FIND "${_registry_text}" "${_expected_include}" _expected_pos)
if(_expected_pos EQUAL -1)
  message(FATAL_ERROR "Expected symlink-preserving include not found in registry header. Wanted '${_expected_include}'.")
endif()

string(FIND "${_registry_text}" "${_forbidden_include}" _forbidden_pos)
if(NOT _forbidden_pos EQUAL -1)
  message(FATAL_ERROR "Registry header leaked real source-tree path via '${_forbidden_include}'.")
endif()

message(STATUS "Symlink mock include generation passed")
