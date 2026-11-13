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

# The package test registration passes the active compilers as PACKAGE_TEST_*.
foreach(_lang IN ITEMS C CXX)
  if((NOT DEFINED ${_lang}_COMPILER OR "${${_lang}_COMPILER}" STREQUAL "")
     AND DEFINED PACKAGE_TEST_${_lang}_COMPILER AND NOT "${PACKAGE_TEST_${_lang}_COMPILER}" STREQUAL "")
    set(${_lang}_COMPILER "${PACKAGE_TEST_${_lang}_COMPILER}")
  endif()
endforeach()

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

# The producer is runtime-only, so stand in for its tool: the lookup only needs
# an existing file. A second prefix holds a decoy that must never be selected.
if(CMAKE_HOST_WIN32)
  set(_exe_ext ".exe")
else()
  set(_exe_ext "")
endif()
set(_installed_tool "${_install_prefix}/bin/gentest_codegen${_exe_ext}")
set(_decoy_prefix "${_work_dir}/decoy")
set(_decoy_tool "${_decoy_prefix}/bin/gentest_codegen${_exe_ext}")
file(WRITE "${_installed_tool}" "stand-in for the installed tool\n")
file(WRITE "${_decoy_tool}" "decoy from another prefix\n")

file(CONFIGURE OUTPUT "${_consumer_source_dir}/CMakeLists.txt" CONTENT [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_install_layout_consumer LANGUAGES CXX)

find_package(gentest CONFIG REQUIRED)
if(NOT COMMAND gentest_attach_codegen)
  message(FATAL_ERROR "find_package(gentest) did not provide gentest_attach_codegen()")
endif()
if(NOT TARGET gentest::gentest_main)
  message(FATAL_ERROR "find_package(gentest) did not provide gentest::gentest_main")
endif()

# PACKAGE_PREFIX_DIR names the prefix of whichever package config is being
# loaded, e.g. when another package's config attaches codegen. It and the
# consumer's own install layout say nothing about where gentest is installed.
set(PACKAGE_PREFIX_DIR "@_decoy_prefix@")
set(CMAKE_INSTALL_BINDIR "not-the-producer-bindir")

add_executable(layout_cases cases.cpp)
gentest_attach_codegen(layout_cases OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
_gentest_find_installed_codegen_executable(resolved_tool)
file(WRITE "${CMAKE_BINARY_DIR}/resolved_tool.txt" "${resolved_tool}")
]=] @ONLY)
file(WRITE "${_consumer_source_dir}/cases.cpp" "int layout_cases_marker = 0;\n")

set(_consumer_cache_args ${_cmake_cache_args}
  "-DCMAKE_PREFIX_PATH=${_install_prefix}"
  "-Dgentest_DIR=${_config_dir}")
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

file(READ "${_consumer_build_dir}/resolved_tool.txt" _resolved_tool)
cmake_path(SET _resolved_tool NORMALIZE "${_resolved_tool}")
cmake_path(SET _expected_tool NORMALIZE "${_installed_tool}")
if(NOT _resolved_tool STREQUAL _expected_tool)
  message(FATAL_ERROR
    "The installed helper must resolve gentest_codegen from its own installation, not from "
    "PACKAGE_PREFIX_DIR or a guessed prefix.\n"
    "Expected: '${_expected_tool}'\n"
    "Resolved: '${_resolved_tool}'")
endif()

# A source-tree helper has no installation and must not probe around the repository.
set(_source_probe "${_work_dir}/source_probe.cmake")
file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir)
file(CONFIGURE OUTPUT "${_source_probe}" CONTENT [=[
set(CMAKE_CROSSCOMPILING FALSE)
set(PACKAGE_PREFIX_DIR "@_decoy_prefix@")
include("@_gentest_source_dir@/cmake/gentest/CodegenToolchain.cmake")
_gentest_find_installed_codegen_executable(resolved_tool)
if(NOT "${resolved_tool}" STREQUAL "")
  message(FATAL_ERROR "The source-tree helper resolved an installed gentest_codegen: '${resolved_tool}'")
endif()
]=] @ONLY)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -P "${_source_probe}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Installed gentest package works with CMAKE_INSTALL_DATADIR=${_datadir}")
