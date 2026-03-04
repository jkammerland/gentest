# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
#  -DPROG=<path to gentest_codegen executable>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DTARGET_ARG=<optional --target=... argument>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckTuRegisterSymbolCollision.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckTuRegisterSymbolCollision.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckTuRegisterSymbolCollision.cmake: GENERATOR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckTuRegisterSymbolCollision.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/tu_register_symbol_collision")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")
set(_generated_dir "${_work_dir}/generated")
file(MAKE_DIRECTORY "${_generated_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
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

message(STATUS "Configure gentest_tu_register_symbol_collision fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_compdb "${_build_dir}/compile_commands.json")
if(NOT EXISTS "${_compdb}")
  message(FATAL_ERROR "Expected CMake to generate '${_compdb}', but it does not exist")
endif()

set(_build_cmd
  "${CMAKE_COMMAND}"
  --build "${_build_dir}"
  --target register_collision_obj)
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_TYPE}")
endif()

message(STATUS "Build gentest_tu_register_symbol_collision fixture target...")
gentest_check_run_or_fail(
  COMMAND
    ${_build_cmd}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_registry "${_work_dir}/mock_registry.hpp")
set(_impl "${_work_dir}/mock_impl.hpp")
file(READ "${_compdb}" _compdb_json)
string(JSON _compdb_entries LENGTH "${_compdb_json}")
if(NOT _compdb_entries EQUAL 2)
  message(FATAL_ERROR "Expected fixture compile_commands.json to have 2 entries, got ${_compdb_entries}")
endif()

set(_alpha_src "")
set(_beta_src "")
foreach(_idx RANGE 0 1)
  string(JSON _entry_file GET "${_compdb_json}" ${_idx} file)
  if(_entry_file MATCHES "[\\\\/]alpha_tu_0042_case\\.cpp$")
    set(_alpha_src "${_entry_file}")
  elseif(_entry_file MATCHES "[\\\\/]beta_tu_0042_case\\.cpp$")
    set(_beta_src "${_entry_file}")
  endif()
endforeach()

if(_alpha_src STREQUAL "" OR _beta_src STREQUAL "")
  message(FATAL_ERROR "Failed to locate alpha_tu_0042_case.cpp/beta_tu_0042_case.cpp entries in '${_compdb}'")
endif()

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()

set(_codegen_cmd
  "${PROG}"
  --mock-registry "${_registry}"
  --mock-impl "${_impl}"
  --tu-out-dir "${_generated_dir}"
  --compdb "${_build_dir}"
  --source-root "${_build_dir}"
  "${_alpha_src}"
  "${_beta_src}")
if(_clang_args)
  list(APPEND _codegen_cmd --)
  list(APPEND _codegen_cmd ${_clang_args})
endif()

gentest_check_run_or_fail(
  COMMAND
    ${_codegen_cmd}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_headers
  "${_generated_dir}/alpha_tu_0042_case.h"
  "${_generated_dir}/beta_tu_0042_case.h")

set(_symbols)
foreach(_header IN LISTS _headers)
  if(NOT EXISTS "${_header}")
    message(FATAL_ERROR "Expected generated header '${_header}' not found")
  endif()
  file(READ "${_header}" _content)
  string(REGEX MATCH "register_tu_[0-9]+" _symbol "${_content}")
  if(_symbol STREQUAL "")
    message(FATAL_ERROR "No register_tu_* symbol found in '${_header}'")
  endif()
  list(APPEND _symbols "${_symbol}")
endforeach()

list(LENGTH _symbols _symbol_count)
set(_unique_symbols ${_symbols})
list(REMOVE_DUPLICATES _unique_symbols)
list(LENGTH _unique_symbols _unique_count)
if(NOT _symbol_count EQUAL _unique_count)
  message(FATAL_ERROR
    "register symbol collision detected for TU outputs. Symbols: ${_symbols}")
endif()

message(STATUS "TU register symbol collision check passed")
