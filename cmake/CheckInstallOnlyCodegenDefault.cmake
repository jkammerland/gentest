# Requires:
#  -DSOURCE_DIR=<fixture source dir>
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

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckInstallOnlyCodegenDefault.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckInstallOnlyCodegenDefault.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckInstallOnlyCodegenDefault.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT EXISTS "${SOURCE_DIR}/target_install_package/CMakeLists.txt")
  message(FATAL_ERROR
    "CheckInstallOnlyCodegenDefault.cmake: expected stub target_install_package fixture at '${SOURCE_DIR}/target_install_package'")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/install_only_codegen_default")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_missing_llvm_dir "${_work_dir}/missing-llvm")
set(_missing_clang_dir "${_work_dir}/missing-clang")
set(_cmake_cache_args
  "-Dgentest_BUILD_TESTING=OFF"
  "-Dgentest_INSTALL=ON"
  "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF"
  "-DFETCHCONTENT_SOURCE_DIR_TARGET_INSTALL_PACKAGE=${SOURCE_DIR}/target_install_package"
  "-DLLVM_DIR=${_missing_llvm_dir}"
  "-DClang_DIR=${_missing_clang_dir}")

if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
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

message(STATUS "Configure install-only producer fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_cache_file "${_build_dir}/CMakeCache.txt")
if(NOT EXISTS "${_cache_file}")
  message(FATAL_ERROR "Expected configure to produce '${_cache_file}'")
endif()

file(STRINGS "${_cache_file}" _codegen_default REGEX "^GENTEST_BUILD_CODEGEN:BOOL=")
if(NOT _codegen_default STREQUAL "GENTEST_BUILD_CODEGEN:BOOL=OFF")
  message(FATAL_ERROR
    "Install-only producer configure must default GENTEST_BUILD_CODEGEN=OFF so packaging builds do not depend on host LLVM/Clang tooling.\n"
    "Observed cache entry: '${_codegen_default}'")
endif()

message(STATUS "Install-only producer configure defaults codegen off")
