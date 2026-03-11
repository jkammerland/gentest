# Requires:
#  -DSOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to parent build dir>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicMockModuleGccFailure.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicMockModuleGccFailure.cmake: BUILD_ROOT not set")
endif()

set(_work_dir "${BUILD_ROOT}/public_mock_module_gcc_failure")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

find_program(_gcc NAMES gcc)
find_program(_gxx NAMES g++)
if(NOT _gcc OR NOT _gxx)
  message(STATUS "Skipping GCC public mock module regression: gcc/g++ not found")
  return()
endif()

execute_process(
  COMMAND "${_gxx}" -dumpfullversion
  OUTPUT_VARIABLE _gxx_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(_gxx_version STREQUAL "")
  execute_process(
    COMMAND "${_gxx}" -dumpversion
    OUTPUT_VARIABLE _gxx_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()

string(REGEX MATCH "^[0-9]+" _gxx_major "${_gxx_version}")
if(_gxx_major STREQUAL "")
  message(STATUS "Skipping GCC public mock module regression: unable to parse g++ version '${_gxx_version}'")
  return()
endif()
if(_gxx_major LESS 15)
  message(STATUS "Skipping GCC public mock module regression: g++ ${_gxx_version} is older than 15")
  return()
endif()

function(_gentest_prepare_ccache_env _out_var)
  set(_ccache_dir "$ENV{CCACHE_DIR}")
  set(_ccache_tmp "$ENV{CCACHE_TEMPDIR}")
  if(_ccache_dir STREQUAL "")
    set(_ccache_dir "${_work_dir}/ccache")
  endif()
  if(_ccache_tmp STREQUAL "")
    set(_ccache_tmp "${_work_dir}/ccache/tmp")
  endif()
  file(MAKE_DIRECTORY "${_ccache_dir}")
  file(MAKE_DIRECTORY "${_ccache_tmp}")
  set(${_out_var}
      "${CMAKE_COMMAND}" -E env
      "CCACHE_DIR=${_ccache_dir}"
      "CCACHE_TEMPDIR=${_ccache_tmp}"
      PARENT_SCOPE)
endfunction()

function(_gentest_run_capture _result_var _output_var)
  set(one_value_args WORKING_DIRECTORY)
  set(multi_value_args COMMAND)
  cmake_parse_arguments(RUN "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "_gentest_run_capture: COMMAND is required")
  endif()

  _gentest_prepare_ccache_env(_env_prefix)
  execute_process(
    COMMAND ${_env_prefix} ${RUN_COMMAND}
    WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(${_result_var} "${_rc}" PARENT_SCOPE)
  set(${_output_var} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

set(_cmake_generator_args)
if(DEFINED GENERATOR AND NOT "${GENERATOR}" STREQUAL "")
  list(APPEND _cmake_generator_args -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _cmake_generator_args -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND _cmake_generator_args -T "${GENERATOR_TOOLSET}")
  endif()
endif()

set(_cmake_cache_args
  "-DCMAKE_C_COMPILER=${_gcc}"
  "-DCMAKE_CXX_COMPILER=${_gxx}"
  "-Dgentest_BUILD_TESTING=OFF"
  "-DGENTEST_BUILD_CODEGEN=OFF")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure GCC producer build for public mock module regression...")
_gentest_run_capture(
  _configure_rc
  _configure_out
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_generator_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}")
if(NOT _configure_rc EQUAL 0)
  message(FATAL_ERROR "Configure failed unexpectedly.\n${_configure_out}")
endif()

message(STATUS "Build gentest_runtime with GCC...")
_gentest_run_capture(
  _build_rc
  _build_out
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target gentest_runtime
  WORKING_DIRECTORY "${_work_dir}")
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR "Expected gentest_runtime to build successfully with GCC, but it failed.\n${_build_out}")
endif()

string(FIND "${_build_out}" "exposes TU-local entity" _tu_local_pos)
if(NOT _tu_local_pos EQUAL -1)
  message(FATAL_ERROR
    "GCC build unexpectedly still reports TU-local entity exposure.\n${_build_out}")
endif()

message(STATUS "GCC public mock module build passed")
