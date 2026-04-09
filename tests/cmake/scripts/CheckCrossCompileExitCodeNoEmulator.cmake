if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCrossCompileExitCodeNoEmulator.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCrossCompileExitCodeNoEmulator.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckCrossCompileExitCodeNoEmulator.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
set(SOURCE_DIR "${_source_dir_norm}")

set(_work_dir "${BUILD_ROOT}/cross_compile_exit_code_no_emulator")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_fixture_src "${_work_dir}/src")
set(_fixture_build "${_work_dir}/build")
file(MAKE_DIRECTORY "${_fixture_src}")
file(MAKE_DIRECTORY "${_fixture_src}/cmake")
file(COPY "${_source_dir_norm}/tests/cmake/cross_compile_emulator_serialization/ProbeScript.cmake"
  DESTINATION "${_fixture_src}")
file(COPY "${_source_dir_norm}/tests/cmake/scripts/CheckExitCode.cmake"
  DESTINATION "${_fixture_src}/cmake")

set(_fixture_cmakelists_template "${_source_dir_norm}/tests/cmake/check_exit_code_no_emulator/CMakeLists.in")
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

string(FIND "${_ctest_content}" "-DEMU=" _emu_pos)
if(NOT _emu_pos EQUAL -1)
  string(SUBSTRING "${_ctest_content}" ${_emu_pos} 160 _emu_excerpt)
  message(FATAL_ERROR
    "gentest_add_check_exit_code(NO_EMULATOR ...) still serialized CMAKE_CROSSCOMPILING_EMULATOR.\n"
    "Observed excerpt:\n${_emu_excerpt}")
endif()

gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${_fixture_build}"
    --output-on-failure
    -R "^exit_code_no_emulator_probe$"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)
