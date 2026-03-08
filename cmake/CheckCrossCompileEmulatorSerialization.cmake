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

  set(_fixture_src "${_work_dir}/${case_name}_src")
  set(_fixture_build "${_work_dir}/${case_name}_build")
  file(MAKE_DIRECTORY "${_fixture_src}")
  file(WRITE "${_fixture_src}/ProbeScript.cmake" "message(STATUS \"probe\")\n")

  set(_emu_lines "")
  foreach(_emu_item IN LISTS CASE_EMULATOR)
    string(APPEND _emu_lines "    \"${_emu_item}\"\n")
  endforeach()

  set(CASE_NAME "${case_name}")
  set(EMU_LINES "${_emu_lines}")
  set(_fixture_cmakelists [=[
cmake_minimum_required(VERSION 3.20)
project(gentest_cross_compile_emulator_serialization_@CASE_NAME@ NONE)
include(CTest)
enable_testing()

include("@SOURCE_DIR@/cmake/GentestTests.cmake")

set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_CROSSCOMPILING_EMULATOR
@EMU_LINES@)

gentest_add_cmake_script_test(
    NAME emu_serialize_probe_@CASE_NAME@
    PROG "${CMAKE_COMMAND}"
    SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ProbeScript.cmake")
]=])
  string(CONFIGURE "${_fixture_cmakelists}" _fixture_cmakelists @ONLY)
  file(WRITE "${_fixture_src}/CMakeLists.txt" "${_fixture_cmakelists}")

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
  string(REGEX MATCH "-DEMU=C:/Program Files/QEMU/qemu-system-aarch64\\.exe[^\\n]*" _emu_segment "${_ctest_content}")
  if(_emu_segment STREQUAL "")
    message(FATAL_ERROR "Could not find -DEMU segment for ${case_name}.\n${_ctest_content}")
  endif()

  string(FIND "${_emu_segment}" ";" _semi_pos)
  if(_semi_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected semicolon-delimited emulator list in ${case_name}, but no semicolon was found.\n"
      "Segment: ${_emu_segment}")
  endif()

  string(FIND "${_ctest_content}" "-DEMU=C:/Program Files/QEMU/qemu-system-aarch64.exe --cpu cortex-a53" _flattened_pos)
  if(CASE_EXPECT_ARGS)
    if(NOT _flattened_pos EQUAL -1)
      message(FATAL_ERROR
        "Emulator was flattened into a space-joined string for ${case_name}.\n"
        "CTestTestfile content:\n${_ctest_content}")
    endif()

    string(FIND "${_emu_segment}" "--cpu" _cpu_pos)
    string(FIND "${_emu_segment}" "cortex-a53" _core_pos)
    if(_cpu_pos EQUAL -1 OR _core_pos EQUAL -1)
      message(FATAL_ERROR
        "Expected emulator arguments were not preserved for ${case_name}.\n"
        "Segment: ${_emu_segment}")
    endif()
  else()
    string(FIND "${_emu_segment}" "--cpu" _cpu_pos)
    string(FIND "${_emu_segment}" "cortex-a53" _core_pos)
    if(NOT _cpu_pos EQUAL -1 OR NOT _core_pos EQUAL -1)
      message(FATAL_ERROR
        "Single-element emulator unexpectedly contained extra args in ${case_name}.\n"
        "Segment: ${_emu_segment}")
    endif()
  endif()
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
