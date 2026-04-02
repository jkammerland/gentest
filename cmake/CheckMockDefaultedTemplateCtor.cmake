# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<top-level build tree>
#  -DGENTEST_SOURCE_DIR=<gentest source tree>
#  -DSOURCE_DIR=<fixture source dir>
#  -DGENERATOR=<cmake generator>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckMockDefaultedTemplateCtor.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckMockDefaultedTemplateCtor.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMockDefaultedTemplateCtor.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMockDefaultedTemplateCtor.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckMockDefaultedTemplateCtor.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/mock_defaulted_template_ctor")
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

set(_cmake_cache_args
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
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
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure mock_defaulted_template_ctor fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

set(_build_cmd
  "${CMAKE_COMMAND}"
  --build "${_build_dir}"
  --target mock_defaulted_template_ctor)
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_TYPE}")
endif()

message(STATUS "Build mock_defaulted_template_ctor fixture...")
gentest_check_run_or_fail(
  COMMAND
    ${_build_cmd}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

message(STATUS "Defaulted template ctor mock fixture built successfully")
