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
#  -DEXPECT_SUBSTRING=<error substring>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenOutputCollision.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenOutputCollision.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckCodegenOutputCollision.cmake: GENERATOR not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "is already used by")
endif()

set(_work_dir "${BUILD_ROOT}/codegen_output_collision")
file(REMOVE_RECURSE "${_work_dir}")

set(_cmd "${CMAKE_COMMAND}")
list(APPEND _cmd -S "${SOURCE_DIR}" -B "${_work_dir}" -G "${GENERATOR}")

if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmd -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmd -T "${GENERATOR_TOOLSET}")
endif()

if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmd "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmd "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmd "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmd "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmd "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

execute_process(
  COMMAND ${_cmd}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

set(_all "${_out}\n${_err}")

if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected configure to fail due to codegen OUTPUT collision, but exit code was 0. Output:\n${_all}")
endif()

string(FIND "${_all}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in output: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
endif()

message(STATUS "Codegen output collision check passed (configure failed with expected message)")

