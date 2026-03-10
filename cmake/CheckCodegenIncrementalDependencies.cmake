# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenIncrementalDependencies.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenIncrementalDependencies.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckCodegenIncrementalDependencies.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenIncrementalDependencies.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/codegen_incremental_deps")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
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
  "-DDEP_SWITCH=0")
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

function(_gentest_read _path _out_var)
  file(READ "${_path}" _text)
  set(${_out_var} "${_text}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_contains _haystack _needle _label)
  string(FIND "${${_haystack}}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${_label}: expected substring not found: '${_needle}'")
  endif()
endfunction()

function(_gentest_expect_not_contains _haystack _needle _label)
  string(FIND "${${_haystack}}" "${_needle}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "${_label}: unexpected substring present: '${_needle}'")
  endif()
endfunction()

message(STATUS "Configure incremental codegen dependency fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

message(STATUS "Initial build...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target dep_tests
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

set(_generated_cpp "${_build_dir}/generated/tu_0000_cases.gentest.h")
set(_mock_registry "${_build_dir}/generated/dep_tests_mock_registry.hpp")
set(_exe "${_build_dir}/dep_tests")

_gentest_read("${_generated_cpp}" _generated_cpp_text)
_gentest_expect_contains(_generated_cpp_text "incremental/compile/off" "initial generated source")
_gentest_expect_not_contains(_generated_cpp_text "incremental/compile/on" "initial generated source")

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _initial_list_out)
_gentest_expect_contains(_initial_list_out "incremental/compile/off" "initial test list")
_gentest_expect_not_contains(_initial_list_out "incremental/compile/on" "initial test list")

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${_src_dir}/iface.hpp"
  "#pragma once\n"
  "\n"
  "namespace depcase {\n"
  "\n"
  "struct Iface {\n"
  "    virtual ~Iface() = default;\n"
  "    virtual void ping(int value) = 0;\n"
  "    virtual int added(int value) const = 0;\n"
  "};\n"
  "\n"
  "} // namespace depcase\n")

message(STATUS "Rebuild after header-only mock interface change...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target dep_tests
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

_gentest_read("${_mock_registry}" _mock_registry_text)
_gentest_expect_contains(_mock_registry_text "int added(int value) const" "mock registry after header change")

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
message(STATUS "Reconfigure after compile-definition change...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
    "-DDEP_SWITCH=1"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

message(STATUS "Rebuild after compile_commands change...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target dep_tests
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

_gentest_read("${_generated_cpp}" _generated_cpp_text_after_reconfigure)
_gentest_expect_contains(_generated_cpp_text_after_reconfigure "incremental/compile/on" "generated source after reconfigure")
_gentest_expect_not_contains(_generated_cpp_text_after_reconfigure "incremental/compile/off" "generated source after reconfigure")

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _final_list_out)
_gentest_expect_contains(_final_list_out "incremental/compile/on" "final test list")
_gentest_expect_not_contains(_final_list_out "incremental/compile/off" "final test list")

message(STATUS "Incremental codegen dependency coverage passed")
