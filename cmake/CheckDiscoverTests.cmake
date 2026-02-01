# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DBUILD_CONFIG=<Debug/Release/...>   # for multi-config generators

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: GENERATOR not set")
endif()

function(run_or_fail)
  set(options "")
  set(oneValueArgs WORKING_DIRECTORY)
  set(multiValueArgs COMMAND)
  cmake_parse_arguments(RUN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  execute_process(
    COMMAND ${RUN_COMMAND}
    WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Command failed (${_rc}): ${RUN_COMMAND}\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}\n")
  endif()

  set(_run_or_fail_out "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

set(_work_dir "${BUILD_ROOT}/discover_tests")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
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
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure gentest_discover_tests fixture...")
run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
)

message(STATUS "Build gentest_discover_tests fixture...")
set(_build_args --build "${_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _build_args --config "${BUILD_CONFIG}")
endif()
run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_build_args} WORKING_DIRECTORY "${_work_dir}")

set(_ctest_cmd "${CMAKE_CTEST_COMMAND}")
set(_ctest_common_args --output-on-failure)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _ctest_common_args -C "${BUILD_CONFIG}")
endif()

message(STATUS "List discovered tests...")
run_or_fail(COMMAND "${_ctest_cmd}" -N ${_ctest_common_args} WORKING_DIRECTORY "${_build_dir}")
set(_list_out "${_run_or_fail_out}")

foreach(_name IN ITEMS "demo/a" "demo/b" "demo/skip" "demo/has [bracket]" "death/demo/death")
  string(FIND "${_list_out}" "${_name}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected discovered test not found: '${_name}'. ctest -N output:\n${_list_out}")
  endif()
endforeach()

if(_list_out MATCHES "(^|\\n)[ \t]*Test #[0-9]+: demo/death([ \t]*$)")
  message(FATAL_ERROR "Death test should not be registered as a normal test: 'demo/death'. ctest -N output:\n${_list_out}")
endif()

message(STATUS "Run discovered tests...")
run_or_fail(COMMAND "${_ctest_cmd}" ${_ctest_common_args} -R "^demo/" WORKING_DIRECTORY "${_build_dir}")

message(STATUS "Run discovered death tests...")
run_or_fail(COMMAND "${_ctest_cmd}" -V ${_ctest_common_args} -R "^death/" WORKING_DIRECTORY "${_build_dir}")
set(_death_out "${_run_or_fail_out}")
string(FIND "${_death_out}" "Death test passed" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected death harness success message. Output:\n${_death_out}")
endif()

message(STATUS "gentest_discover_tests fixture passed")
