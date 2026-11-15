# Configures an installing gentest with GENTEST_USE_INSTALLED_PACKAGE_HELPER=ON
# against a copy of the bundled target_install_package placed under another
# prefix, as an installed helper would be, and checks that configure used that
# copy rather than the bundled one.
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
  message(FATAL_ERROR "CheckInstalledPackageHelper.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckInstalledPackageHelper.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

# A cross toolchain re-roots package searches into its find root, where this
# test cannot place an installed helper for every toolchain.
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  gentest_skip_test("installed package helper check: a toolchain file re-roots package searches")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/installed_package_helper")
set(_helper_prefix "${_work_dir}/helper-prefix")
set(_build_dir "${_work_dir}/p")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
# Stand in for an installed helper: the bundled install tree under another prefix.
file(COPY "${GENTEST_SOURCE_DIR}/third_party/target_install_package/" DESTINATION "${_helper_prefix}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  gentest_append_windows_native_llvm_cache_args(_cmake_cache_args "${CXX_COMPILER}" ${_cmake_cache_args})
endif()

message(STATUS "Configure an installing producer that uses the helper installed under '${_helper_prefix}'...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=OFF"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=OFF"
    "-DGENTEST_USE_INSTALLED_PACKAGE_HELPER=ON"
    "-DCMAKE_PREFIX_PATH=${_helper_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(STRINGS "${_build_dir}/CMakeCache.txt" _helper_dir_line REGEX "^target_install_package_DIR:PATH=" LIMIT_COUNT 1)
string(REGEX REPLACE "^target_install_package_DIR:PATH=" "" _helper_dir "${_helper_dir_line}")
if("${_helper_dir}" STREQUAL "")
  message(FATAL_ERROR "Configure recorded no target_install_package_DIR in '${_build_dir}/CMakeCache.txt'")
endif()
file(REAL_PATH "${_helper_dir}" _helper_dir_real)
file(REAL_PATH "${_helper_prefix}" _helper_prefix_real)
cmake_path(IS_PREFIX _helper_prefix_real "${_helper_dir_real}" NORMALIZE _found_installed_copy)
if(NOT _found_installed_copy)
  message(FATAL_ERROR
    "With GENTEST_USE_INSTALLED_PACKAGE_HELPER=ON, configure used target_install_package from '${_helper_dir}' "
    "instead of the installed copy under '${_helper_prefix}'.")
endif()

message(STATUS "Installed package helper check passed: ${_helper_dir}")
