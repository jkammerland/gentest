# Validates that gentest_add_cmake_script_test serializes CMAKE_CROSSCOMPILING_EMULATOR
# as a semicolon-delimited list (preserving spaces in executable paths) rather than
# flattening it to a space-joined string.
#
# Requires:
#  -DSOURCE_DIR=<path to gentest source root>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCrossCompileEmulatorSerialization.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCrossCompileEmulatorSerialization.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckCrossCompileEmulatorSerialization.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include(CMakeParseArguments)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
set(SOURCE_DIR "${_source_dir_norm}")

set(_work_dir "${BUILD_ROOT}/cross_compile_emulator_serialization")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

function(_gentest_check_emu_case case_name)
  set(options EXPECT_ARGS)
  set(oneValueArgs)
  set(multiValueArgs EMULATOR)
  cmake_parse_arguments(CASE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CASE_EMULATOR)
    message(FATAL_ERROR "CheckCrossCompileEmulatorSerialization: ${case_name} missing EMULATOR entries")
  endif()
  list(GET CASE_EMULATOR 0 _expected_emu)
  list(LENGTH CASE_EMULATOR _expected_emu_count)

  set(_fixture_src "${_work_dir}/${case_name}_src")
  set(_fixture_build "${_work_dir}/${case_name}_build")
  file(MAKE_DIRECTORY "${_fixture_src}")
  file(COPY "${_source_dir_norm}/tests/cmake/cross_compile_emulator_serialization/ProbeScript.cmake"
    DESTINATION "${_fixture_src}")

  set(_emu_lines "")
  foreach(_emu_item IN LISTS CASE_EMULATOR)
    string(APPEND _emu_lines "    \"${_emu_item}\"\n")
  endforeach()

  set(CASE_NAME "${case_name}")
  set(EMU_LINES "${_emu_lines}")
  set(_fixture_cmakelists_template "${_source_dir_norm}/tests/cmake/cross_compile_emulator_serialization/CMakeLists.in")
  set(_fixture_cmakelists "${_fixture_src}/CMakeLists.txt")
  configure_file("${_fixture_cmakelists_template}" "${_fixture_cmakelists}" @ONLY)

  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_fixture_src}"
      -B "${_fixture_build}"
    STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${_work_dir}"
  )

  set(_ctest_file "${_fixture_build}/CTestTestfile.cmake")
  if(NOT EXISTS "${_ctest_file}")
    message(FATAL_ERROR "Expected file not found: ${_ctest_file}")
  endif()

  file(READ "${_ctest_file}" _ctest_content)
  string(REGEX MATCH "\"-DEMU=[^\"]*\"" _emu_segment "${_ctest_content}")
  if(_emu_segment STREQUAL "")
    string(REGEX MATCH "(^|[ \\t\\(])(-DEMU=[^ \\t\\r\\n)]*)" _emu_segment_with_sep "${_ctest_content}")
    set(_emu_segment "${CMAKE_MATCH_2}")
  endif()
  if(_emu_segment STREQUAL "")
    message(FATAL_ERROR "Could not find -DEMU segment for ${case_name}.\n${_ctest_content}")
  endif()

  if(_emu_segment MATCHES "^\\\".*\\\"$")
    string(REGEX REPLACE "^\\\"-DEMU=|\\\"$" "" _emu_value "${_emu_segment}")
  else()
    string(REPLACE "-DEMU=" "" _emu_value "${_emu_segment}")
  endif()

  cmake_policy(PUSH)
  cmake_policy(SET CMP0007 NEW)
  set(_emu_list "${_emu_value}")

  list(LENGTH _emu_list _emu_arg_count)
  if(_emu_arg_count EQUAL 0)
    message(FATAL_ERROR
      "No emulator command arguments were found for ${case_name}.\n"
      "Segment: ${_emu_segment}")
  endif()

  if(_emu_arg_count GREATER 0)
    math(EXPR _emu_last_index "${_emu_arg_count} - 1")
    list(GET _emu_list ${_emu_last_index} _emu_last_item)
    if(_emu_last_item STREQUAL "")
      list(REMOVE_AT _emu_list ${_emu_last_index})
      list(LENGTH _emu_list _emu_arg_count)
    endif()
  endif()

  list(JOIN CASE_EMULATOR ";" _expected_emu_joined)
  list(JOIN _emu_list ";" _emu_joined)
  if(NOT _emu_joined STREQUAL _expected_emu_joined)
    message(FATAL_ERROR
      "CMake test command did not serialize the emulator list correctly for ${case_name}.\n"
      "Expected: ${_expected_emu_joined}\n"
      "Observed: ${_emu_joined}\n"
      "Segment: ${_emu_segment}")
  endif()

  if(CASE_EXPECT_ARGS)
    if(NOT _emu_arg_count EQUAL _expected_emu_count)
      message(FATAL_ERROR
        "Expected ${_expected_emu_count} emulator list elements for ${case_name}, got ${_emu_arg_count}.\n"
        "Segment: ${_emu_segment}")
    endif()

    list(GET _emu_list 0 _actual_first_arg)
    if(_actual_first_arg STREQUAL "")
      message(FATAL_ERROR
        "Expected emulator executable path was empty for ${case_name}.\n"
        "Segment: ${_emu_segment}")
    endif()

    list(JOIN CASE_EMULATOR " " _expected_emu_joined_space)
    set(_flat_test_fragment "-DEMU=${_expected_emu_joined_space}")
    string(FIND "${_ctest_content}" "${_flat_test_fragment}" _flattened_pos)
    if(NOT _flattened_pos EQUAL -1)
      message(FATAL_ERROR
        "Emulator was flattened into a space-joined string for ${case_name}.\n"
        "CTestTestfile content:\n${_ctest_content}")
    endif()
  else()
    if(_emu_arg_count GREATER 1)
      message(FATAL_ERROR
        "Single-element emulator unexpectedly contained extra semicolon-delimited args in ${case_name}.\n"
        "Segment: ${_emu_segment}")
    endif()
    list(GET _emu_list 0 _actual_first_arg)
    if(NOT _actual_first_arg STREQUAL _expected_emu)
      message(FATAL_ERROR
        "Single-element emulator unexpectedly rewrote executable path for ${case_name}.\n"
        "Expected '${_expected_emu}', observed '${_actual_first_arg}'."
        "Segment: ${_emu_segment}")
    endif()
  endif()
  cmake_policy(POP)
endfunction()

_gentest_check_emu_case(
  multi
  EXPECT_ARGS
  EMULATOR
    "C:/Program Files/QEMU/qemu-system-aarch64.exe"
    "--cpu"
    "cortex-a53")

_gentest_check_emu_case(
  single
  EMULATOR
    "C:/Program Files/QEMU/qemu-system-aarch64.exe")
