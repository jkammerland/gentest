# Installs a runtime-only gentest with a non-default CMAKE_INSTALL_DATADIR and
# checks that the installed package still works from that layout.
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
#  -DBUILD_CONFIG=<Debug|Release|...>   (multi-config generators)

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageInstallLayout.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageInstallLayout.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/package_install_layout")
set(_producer_build_dir "${_work_dir}/p")
set(_install_prefix "${_work_dir}/prefix")
set(_consumer_source_dir "${_work_dir}/consumer")
set(_consumer_build_dir "${_work_dir}/c")
# Deliberately not one directory deep, unlike the default "share".
set(_datadir "data/gentest-layout")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_consumer_source_dir}")

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
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  gentest_append_windows_native_llvm_cache_args(_cmake_cache_args "${CXX_COMPILER}")
endif()

message(STATUS "Configure a runtime-only producer with CMAKE_INSTALL_DATADIR=${_datadir}...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_cmake_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=OFF"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=OFF"
    "-DCMAKE_INSTALL_DATADIR=${_datadir}"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_producer_build_args --build "${_producer_build_dir}" --target install)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _producer_build_args --config "${BUILD_CONFIG}")
endif()
message(STATUS "Build and install the producer into '${_install_prefix}'...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_producer_build_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_config_dir "${_install_prefix}/${_datadir}/cmake/gentest")
foreach(_installed IN ITEMS
    "${_config_dir}/gentestConfig.cmake"
    "${_config_dir}/GentestCodegen.cmake"
    "${_config_dir}/gentest/CodegenToolchain.cmake"
    "${_config_dir}/gentest/TuMode.cmake")
  if(NOT EXISTS "${_installed}")
    message(FATAL_ERROR
      "Expected '${_installed}' in the install tree: GentestCodegen.cmake and the gentest/*.cmake helpers it "
      "includes must be installed together under CMAKE_INSTALL_DATADIR.")
  endif()
endforeach()
if(EXISTS "${_install_prefix}/share/cmake/gentest")
  message(FATAL_ERROR
    "CMake helpers were installed under the hard-coded '${_install_prefix}/share/cmake/gentest' although "
    "CMAKE_INSTALL_DATADIR is '${_datadir}'.")
endif()

set(_producer_fmt_dir "")
file(STRINGS "${_producer_build_dir}/CMakeCache.txt" _producer_fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
if(_producer_fmt_dir_line)
  string(REGEX REPLACE "^fmt_DIR:PATH=" "" _producer_fmt_dir "${_producer_fmt_dir_line}")
endif()

file(WRITE "${_consumer_source_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_install_layout_consumer LANGUAGES CXX)

find_package(gentest CONFIG REQUIRED)
if(NOT COMMAND gentest_attach_codegen)
  message(FATAL_ERROR "find_package(gentest) did not provide gentest_attach_codegen()")
endif()
if(NOT TARGET gentest::gentest_main)
  message(FATAL_ERROR "find_package(gentest) did not provide gentest::gentest_main")
endif()
]=])

set(_consumer_cache_args ${_cmake_cache_args} "-DCMAKE_PREFIX_PATH=${_install_prefix}" "-Dgentest_DIR=${_config_dir}")
if(NOT "${_producer_fmt_dir}" STREQUAL "" AND IS_DIRECTORY "${_producer_fmt_dir}")
  list(APPEND _consumer_cache_args "-Dfmt_DIR=${_producer_fmt_dir}")
endif()

message(STATUS "Configure a find_package(gentest) consumer...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_consumer_source_dir}"
    -B "${_consumer_build_dir}"
    ${_consumer_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Installed gentest package works with CMAKE_INSTALL_DATADIR=${_datadir}")
