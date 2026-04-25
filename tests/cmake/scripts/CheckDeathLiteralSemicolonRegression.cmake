if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckDeathLiteralSemicolonRegression.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_TESTS_MODULE)
  message(FATAL_ERROR "CheckDeathLiteralSemicolonRegression.cmake: GENTEST_TESTS_MODULE not set")
endif()

set(_cmake_gen_args)
if(DEFINED GENERATOR AND NOT "${GENERATOR}" STREQUAL "")
  list(APPEND _cmake_gen_args -G "${GENERATOR}")
endif()
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

set(_work_dir "${BUILD_ROOT}/check_death_literal_semicolon")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/src")

if(WIN32)
  set(_fake_death "${_work_dir}/src/fake_death.bat")
  file(WRITE "${_fake_death}" "@echo off\r\necho alpha\r\necho beta\r\nexit /b 1\r\n")
else()
  set(_fake_death "${_work_dir}/src/fake_death.sh")
  file(WRITE "${_fake_death}" "#!/bin/sh\nprintf '%s\\n' alpha beta\nexit 1\n")
  file(CHMOD "${_fake_death}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

file(WRITE "${_work_dir}/src/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.25)
project(check_death_literal_semicolon NONE)
enable_testing()
include(\"${GENTEST_TESTS_MODULE}\")
gentest_add_check_death(
  NAME literal_semicolon_is_single_required_substring
  NO_EMULATOR
  PROG \"${_fake_death}\"
  REQUIRED_SUBSTRINGS \"alpha\\;beta\")
")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${_work_dir}/src" -B "${_work_dir}/build" ${_cmake_gen_args} ${_cmake_cache_args}
  RESULT_VARIABLE _configure_rc
  OUTPUT_VARIABLE _configure_out
  ERROR_VARIABLE _configure_err)
if(NOT _configure_rc EQUAL 0)
  message(FATAL_ERROR
    "Nested literal-semicolon configure failed.\n--- stdout ---\n${_configure_out}\n--- stderr ---\n${_configure_err}")
endif()

set(_ctest_args --test-dir "${_work_dir}/build" --output-on-failure -R literal_semicolon_is_single_required_substring)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _ctest_args -C "${BUILD_CONFIG}")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" ${_ctest_args}
  RESULT_VARIABLE _ctest_rc
  OUTPUT_VARIABLE _ctest_out
  ERROR_VARIABLE _ctest_err)
if(_ctest_rc EQUAL 0)
  message(FATAL_ERROR
    "gentest_add_check_death accepted split substrings for a literal semicolon. The nested check should fail because output contains separate 'alpha' and 'beta' lines, not 'alpha;beta'.\n--- stdout ---\n${_ctest_out}\n--- stderr ---\n${_ctest_err}")
endif()

set(_ctest_all "${_ctest_out}\n${_ctest_err}")
string(FIND "${_ctest_all}" "alpha;beta" _literal_pos)
if(_literal_pos EQUAL -1)
  message(FATAL_ERROR
    "Nested check failed, but not while looking for the literal semicolon substring 'alpha;beta'.\n--- stdout ---\n${_ctest_out}\n--- stderr ---\n${_ctest_err}")
endif()
