# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTimestampPreservation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTimestampPreservation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTimestampPreservation.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTimestampPreservation.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTimestampPreservation.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

if(NOT "${GENERATOR}" MATCHES "Ninja|Makefiles")
  gentest_skip_test("explicit mock timestamp regression requires Ninja or Makefiles")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/explicit_mock_timestamp_preservation")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DGENTEST_BUILD_CODEGEN=OFF"
  "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

function(_gentest_snapshot_mtimes out_var)
  set(_snapshot "")
  foreach(_path IN LISTS ARGN)
    if(NOT EXISTS "${_path}")
      message(FATAL_ERROR "Expected generated artifact was not created: ${_path}")
    endif()
    file(TIMESTAMP "${_path}" _mtime "%s.%f" UTC)
    if("${_mtime}" STREQUAL "")
      message(FATAL_ERROR "Unable to read generated artifact timestamp: ${_path}")
    endif()
    list(APPEND _snapshot "${_path}|${_mtime}")
  endforeach()
  set(${out_var} "${_snapshot}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_same_mtimes snapshot label)
  foreach(_entry IN LISTS snapshot)
    string(REPLACE "|" ";" _fields "${_entry}")
    list(GET _fields 0 _path)
    list(GET _fields 1 _expected_mtime)
    if(NOT EXISTS "${_path}")
      message(FATAL_ERROR "${label}: generated artifact disappeared: ${_path}")
    endif()
    file(TIMESTAMP "${_path}" _actual_mtime "%s.%f" UTC)
    if(NOT "${_actual_mtime}" STREQUAL "${_expected_mtime}")
      message(FATAL_ERROR
        "${label}: '${_path}' changed timestamp from '${_expected_mtime}' to '${_actual_mtime}'")
    endif()
  endforeach()
endfunction()

message(STATUS "Configure explicit mock timestamp fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_gen_args} -S "${_src_dir}" -B "${_build_dir}" ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

message(STATUS "Initial build of explicit mock timestamp fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target timestamp_consumer
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

set(_generated_root "${_build_dir}/generated artifacts with spaces")
set(_mock_output_dir "${_generated_root}/mock artifacts")
set(_consumer_output_dir "${_generated_root}/consumer artifacts")
file(GLOB_RECURSE _generated_outputs LIST_DIRECTORIES false
  "${_mock_output_dir}/*"
  "${_consumer_output_dir}/*")
list(FILTER _generated_outputs EXCLUDE REGEX "/compdb/compile_commands\\.staged$")
if(NOT _generated_outputs)
  message(FATAL_ERROR "Expected generated outputs below '${_generated_root}'")
endif()
set(_staged_defs_glob "${_mock_output_dir}/defs/*_timestamp_mock_defs.hpp")
file(GLOB _staged_defs ${_staged_defs_glob})
list(LENGTH _staged_defs _staged_defs_count)
if(NOT _staged_defs_count EQUAL 1)
  message(FATAL_ERROR "Expected one staged mock defs output matching '${_staged_defs_glob}', found ${_staged_defs_count}: ${_staged_defs}")
endif()
set(_public_header "${_mock_output_dir}/public headers/timestamp mocks.hpp")
file(GLOB _textual_wrapper "${_mock_output_dir}/timestamp_mocks_*_defs.cpp")
file(GLOB _anchor "${_mock_output_dir}/timestamp_mocks_*_anchor.cpp")
list(LENGTH _textual_wrapper _textual_wrapper_count)
list(LENGTH _anchor _anchor_count)
if(NOT _textual_wrapper_count EQUAL 1 OR NOT _anchor_count EQUAL 1)
  message(FATAL_ERROR
    "Expected one generated textual wrapper and anchor under '${_mock_output_dir}', found "
    "${_textual_wrapper_count} and ${_anchor_count}: ${_textual_wrapper}; ${_anchor}")
endif()
if(WIN32)
  set(_consumer_executable "${_build_dir}/timestamp_consumer.exe")
else()
  set(_consumer_executable "${_build_dir}/timestamp_consumer")
endif()
_gentest_snapshot_mtimes(
  _initial_mtimes
  ${_generated_outputs}
  "${_public_header}"
  "${_textual_wrapper}"
  "${_anchor}"
  "${_consumer_executable}")
file(TIMESTAMP "${_staged_defs}" _staged_defs_initial_mtime "%s.%f" UTC)

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
message(STATUS "Reconfigure explicit mock timestamp fixture without changes...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_gen_args} -S "${_src_dir}" -B "${_build_dir}" ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")
_gentest_expect_same_mtimes("${_initial_mtimes}" "unchanged explicit mock reconfigure")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target timestamp_consumer --verbose
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _noop_build_rc
  OUTPUT_VARIABLE _noop_build_out
  ERROR_VARIABLE _noop_build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _noop_build_rc EQUAL 0)
  message(FATAL_ERROR
    "No-op explicit mock build failed.\n--- stdout ---\n${_noop_build_out}\n--- stderr ---\n${_noop_build_err}")
endif()
set(_noop_build_text "${_noop_build_out}\n${_noop_build_err}")
# CMake regenerates its raw compile database during configure. The staging
# edge may therefore run once, but it must not propagate to codegen,
# compilation, or linking.
foreach(_forbidden_edge IN ITEMS "${PROG}" "Building CXX object" "Linking CXX")
  string(FIND "${_noop_build_text}" "${_forbidden_edge}" _forbidden_edge_pos)
  if(NOT _forbidden_edge_pos EQUAL -1)
    message(FATAL_ERROR
      "No-op build unexpectedly ran '${_forbidden_edge}'.\n${_noop_build_text}")
  endif()
endforeach()
_gentest_expect_same_mtimes("${_initial_mtimes}" "unchanged explicit mock no-op build")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target timestamp_consumer --verbose
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _settled_build_rc
  OUTPUT_VARIABLE _settled_build_out
  ERROR_VARIABLE _settled_build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _settled_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Settled no-op explicit mock build failed.\n--- stdout ---\n${_settled_build_out}\n--- stderr ---\n${_settled_build_err}")
endif()
set(_settled_build_text "${_settled_build_out}\n${_settled_build_err}")
foreach(_forbidden_edge IN ITEMS "Staging compile commands" "${PROG}" "Building CXX object" "Linking CXX")
  string(FIND "${_settled_build_text}" "${_forbidden_edge}" _forbidden_edge_pos)
  if(NOT _forbidden_edge_pos EQUAL -1)
    message(FATAL_ERROR
      "Settled no-op build unexpectedly ran '${_forbidden_edge}'.\n${_settled_build_text}")
  endif()
endforeach()
_gentest_expect_same_mtimes("${_initial_mtimes}" "settled explicit mock no-op build")

# Content changes must still be published to the staged source surface.
file(READ "${_src_dir}/timestamp_mock_defs.hpp" _timestamp_defs_source)
string(APPEND _timestamp_defs_source "\n// timestamp preservation content change\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${_src_dir}/timestamp_mock_defs.hpp" "${_timestamp_defs_source}")
message(STATUS "Reconfigure explicit mock timestamp fixture after a source change...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_gen_args} -S "${_src_dir}" -B "${_build_dir}" ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")
file(TIMESTAMP "${_staged_defs}" _staged_defs_changed_mtime "%s.%f" UTC)
if("${_staged_defs_changed_mtime}" STREQUAL "${_staged_defs_initial_mtime}")
  message(FATAL_ERROR "Changed explicit mock defs did not update staged output: ${_staged_defs}")
endif()
file(READ "${_staged_defs}" _staged_defs_content)
if(NOT _staged_defs_content MATCHES "timestamp preservation content change")
  message(FATAL_ERROR "Changed explicit mock defs content was not published: ${_staged_defs}")
endif()

message(STATUS "Explicit mock timestamp preservation regression passed")
