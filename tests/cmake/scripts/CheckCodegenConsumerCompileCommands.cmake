# A consumer that never sets CMAKE_EXPORT_COMPILE_COMMANDS must still get a
# compile database entry for the target it attaches codegen to: codegen takes
# the target's flags from there.
#
# Requires:
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenConsumerCompileCommands.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenConsumerCompileCommands.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

if(NOT "${GENERATOR}" MATCHES "Ninja|Makefiles")
  gentest_skip_test("generator '${GENERATOR}' does not write compile_commands.json; codegen uses a synthetic compile context")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/codegen_consumer_compile_commands")
set(_source_dir "${_work_dir}/consumer")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_source_dir}")

file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir)
# Like a find_package() consumer: only the helper is loaded, and nothing in the
# project asks for a compile database.
file(CONFIGURE OUTPUT "${_source_dir}/CMakeLists.txt" CONTENT [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_consumer_compile_commands LANGUAGES CXX)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  message(FATAL_ERROR "fixture must not enable CMAKE_EXPORT_COMPILE_COMMANDS")
endif()

include("@_gentest_source_dir@/cmake/GentestCodegen.cmake")
# Configure only: any existing program satisfies the host tool selection.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

add_executable(consumer_cases consumer_cases.cpp)
target_compile_definitions(consumer_cases PRIVATE GENTEST_CONSUMER_ONLY_FLAG=1)
gentest_attach_codegen(consumer_cases OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_library(unrelated STATIC unrelated.cpp)
]=] @ONLY)
file(WRITE "${_source_dir}/consumer_cases.cpp" "int consumer_cases_marker = 0;\n")
file(WRITE "${_source_dir}/unrelated.cpp" "int unrelated_marker = 0;\n")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

# The outer environment must not provide the setting either.
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env --unset=CMAKE_EXPORT_COMPILE_COMMANDS
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_source_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_compdb "${_build_dir}/compile_commands.json")
if(NOT EXISTS "${_compdb}")
  message(FATAL_ERROR
    "gentest_attach_codegen() must make CMake write '${_compdb}' even when the consumer does not set "
    "CMAKE_EXPORT_COMPILE_COMMANDS; the codegen build step depends on it.")
endif()

file(READ "${_compdb}" _compdb_json)
string(JSON _entry_count LENGTH "${_compdb_json}")
set(_found_cases FALSE)
set(_found_unrelated FALSE)
if(_entry_count GREATER 0)
  math(EXPR _last_entry "${_entry_count} - 1")
  foreach(_idx RANGE 0 ${_last_entry})
    string(JSON _file GET "${_compdb_json}" ${_idx} file)
    string(JSON _command ERROR_VARIABLE _command_error GET "${_compdb_json}" ${_idx} command)
    if(_file MATCHES "/consumer_cases\\.cpp$")
      set(_found_cases TRUE)
      if(NOT _command MATCHES "GENTEST_CONSUMER_ONLY_FLAG=1")
        message(FATAL_ERROR "The compile database entry for consumer_cases.cpp lacks the target's flags:\n${_command}")
      endif()
    elseif(_file MATCHES "/unrelated\\.cpp$")
      set(_found_unrelated TRUE)
    endif()
  endforeach()
endif()
if(NOT _found_cases)
  message(FATAL_ERROR
    "compile_commands.json has no entry for the codegen target's consumer_cases.cpp; codegen would parse it "
    "without the target's flags.\n${_compdb_json}")
endif()
if(_found_unrelated)
  message(FATAL_ERROR
    "gentest_attach_codegen() must only export compile commands for the target it is attached to, not for the "
    "consumer's other targets.")
endif()

message(STATUS "gentest_attach_codegen() exports compile commands for its target without CMAKE_EXPORT_COMPILE_COMMANDS")
