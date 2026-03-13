# Usage:
#   cmake
#     -DSOURCE_DIR=<path>
#     -DBUILD_ROOT=<path>
#     -DPACKAGE_NAME=<name>
#     [-DGENERATOR=<cmake generator>]
#     [-DGENERATOR_PLATFORM=<platform>]
#     [-DGENERATOR_TOOLSET=<toolset>]
#     [-DTOOLCHAIN_FILE=<toolchain.cmake>]
#     [-DMAKE_PROGRAM=<path>]
#     [-DC_COMPILER=<path>]
#     [-DCXX_COMPILER=<path>]
#     [-DPACKAGE_TEST_C_COMPILER=<path>]
#     [-DPACKAGE_TEST_CXX_COMPILER=<path>]
#     [-DCONSUMER_LINK_MODE=<main_only|runtime_only|double>]
#     [-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_WORK_DIR=<ON|OFF>]
#     [-DBUILD_TYPE=<Debug|Release|...>]
#     [-DBUILD_CONFIG=<Debug|Release|...>]   # for multi-config generators
#     -P cmake/CheckPackageConsumer.cmake

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME)
  message(FATAL_ERROR "PACKAGE_NAME not set")
endif()
if(NOT DEFINED CONSUMER_LINK_MODE OR "${CONSUMER_LINK_MODE}" STREQUAL "")
  set(CONSUMER_LINK_MODE "double")
endif()
if(NOT DEFINED PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE OR "${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}" STREQUAL "")
  set(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE ON)
endif()
if(NOT DEFINED PACKAGE_TEST_DRY_RUN_WORK_DIR OR "${PACKAGE_TEST_DRY_RUN_WORK_DIR}" STREQUAL "")
  set(PACKAGE_TEST_DRY_RUN_WORK_DIR OFF)
endif()
if(NOT DEFINED PROG)
  set(PROG "")
endif()
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE AND NOT PACKAGE_TEST_DRY_RUN_WORK_DIR AND "${PROG}" STREQUAL "")
  message(FATAL_ERROR
    "CheckPackageConsumer.cmake: PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON requires PROG to point at gentest_codegen")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

function(run_or_fail)
  set(options "")
  set(oneValueArgs WORKING_DIRECTORY)
  set(multiValueArgs COMMAND)
  cmake_parse_arguments(RUN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "run_or_fail: COMMAND is required")
  endif()

  # The consumer test performs nested builds. Some environments configure ccache
  # with an unwritable temp directory (e.g. sandboxed /run/user/...); provide a
  # deterministic temp location under the test's build root unless the caller
  # already configured one.
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

  set(_command
    "${CMAKE_COMMAND}" -E env
      "CCACHE_DIR=${_ccache_dir}"
      "CCACHE_TEMPDIR=${_ccache_tmp}"
      ${RUN_COMMAND})

  gentest_check_run_or_fail(
    COMMAND ${_command}
    WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
    DISPLAY_COMMAND "${RUN_COMMAND}"
  )
endfunction()

set(_exe_ext "")
if(CMAKE_HOST_WIN32)
  set(_exe_ext ".exe")
endif()

set(_cmake_generator_args)
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  list(APPEND _cmake_generator_args -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _cmake_generator_args -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _cmake_generator_args -T "${GENERATOR_TOOLSET}")
  endif()
endif()

set(_cmake_cache_args)
if(DEFINED TOOLCHAIN_FILE AND NOT TOOLCHAIN_FILE STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT MAKE_PROGRAM STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
set(_effective_c_compiler "")
if(DEFINED PACKAGE_TEST_C_COMPILER AND NOT PACKAGE_TEST_C_COMPILER STREQUAL "")
  set(_effective_c_compiler "${PACKAGE_TEST_C_COMPILER}")
elseif(DEFINED C_COMPILER AND NOT C_COMPILER STREQUAL "")
  set(_effective_c_compiler "${C_COMPILER}")
endif()

set(_effective_cxx_compiler "")
if(DEFINED PACKAGE_TEST_CXX_COMPILER AND NOT PACKAGE_TEST_CXX_COMPILER STREQUAL "")
  set(_effective_cxx_compiler "${PACKAGE_TEST_CXX_COMPILER}")
elseif(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
  set(_effective_cxx_compiler "${CXX_COMPILER}")
endif()

set(_effective_cxx_compiler_name "")
if(NOT _effective_cxx_compiler STREQUAL "")
  get_filename_component(_effective_cxx_compiler_name "${_effective_cxx_compiler}" NAME)
endif()

set(_effective_build_type "${BUILD_TYPE}")
if(CMAKE_HOST_WIN32
   AND NOT _effective_cxx_compiler STREQUAL ""
   AND _effective_cxx_compiler MATCHES "[Cc]lang"
   AND NOT _effective_cxx_compiler_name MATCHES "^clang-cl(\\.exe)?$")
  # CMake's synthetic imported-module build for GNU-style clang still injects
  # the Debug DLL runtime in Debug builds, which then conflicts with the
  # consumer shims. Drive this installed-package smoke through Release on that
  # platform/toolchain combination so the module synth units and consumer code
  # share one CRT model.
  set(_effective_build_type "Release")
endif()

if(NOT _effective_c_compiler STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${_effective_c_compiler}")
endif()
if(NOT _effective_cxx_compiler STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_BUILD_CODEGEN=OFF")
endif()

set(_work_dir_suffix "${PACKAGE_NAME}_${CONSUMER_LINK_MODE}")
if(NOT _effective_cxx_compiler_name STREQUAL "")
  string(REGEX REPLACE "[^A-Za-z0-9_.-]+" "_" _compiler_tag "${_effective_cxx_compiler_name}")
  string(APPEND _work_dir_suffix "_${_compiler_tag}")
endif()
if(NOT "${_effective_build_type}" STREQUAL "")
  string(APPEND _work_dir_suffix "_${_effective_build_type}")
endif()
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  string(APPEND _work_dir_suffix "_${BUILD_CONFIG}")
endif()
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE)
  string(APPEND _work_dir_suffix "_external_codegen")
else()
  string(APPEND _work_dir_suffix "_native_codegen")
endif()
set(_work_dir "${BUILD_ROOT}/package_consumer_${_work_dir_suffix}")
set(_producer_build_dir "${_work_dir}/producer")
set(_install_prefix "${_work_dir}/install")
set(_consumer_build_dir "${_work_dir}/consumer")
set(_consumer_source_dir "${_work_dir}/consumer_src")

if(PACKAGE_TEST_DRY_RUN_WORK_DIR)
  message(STATUS "CheckPackageConsumer work dir: ${_work_dir}")
  return()
endif()

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/tests/consumer/" DESTINATION "${_consumer_source_dir}")

message(STATUS "Configure producer build (${PACKAGE_NAME})...")
run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_generator_args}
    -S "${SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_cmake_cache_args}
    "-D${PACKAGE_NAME}_INSTALL=ON"
    "-D${PACKAGE_NAME}_BUILD_TESTING=OFF"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}")

message(STATUS "Build and install producer into '${_install_prefix}'...")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  # Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) often
  # generate for several configurations at once. Install both Debug and Release
  # so imported targets have locations for common configs (and for the config
  # mapping logic in the generated *Config.cmake files).
  set(_install_configs Debug Release)
  list(REMOVE_DUPLICATES _install_configs)
  foreach(_cfg IN LISTS _install_configs)
    run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install --config "${_cfg}")
  endforeach()
else()
  run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install)
endif()

set(_producer_fmt_dir "")
set(_producer_cache_file "${_producer_build_dir}/CMakeCache.txt")
if(EXISTS "${_producer_cache_file}")
  file(STRINGS "${_producer_cache_file}" _producer_fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
  if(_producer_fmt_dir_line)
    list(GET _producer_fmt_dir_line 0 _producer_fmt_dir_line_value)
    string(REGEX REPLACE "^fmt_DIR:PATH=" "" _producer_fmt_dir "${_producer_fmt_dir_line_value}")
    unset(_producer_fmt_dir_line_value)
  endif()
endif()

message(STATUS "Locate installed '${PACKAGE_NAME}' CMake package...")
file(GLOB_RECURSE _config_candidates
  LIST_DIRECTORIES FALSE
  "${_install_prefix}/*/${PACKAGE_NAME}Config.cmake"
  "${_install_prefix}/*/*/${PACKAGE_NAME}Config.cmake")
if(NOT _config_candidates)
  message(FATAL_ERROR "Installed package config '${PACKAGE_NAME}Config.cmake' not found under '${_install_prefix}'")
endif()
list(GET _config_candidates 0 _config_file)
get_filename_component(_config_dir "${_config_file}" DIRECTORY)
file(GLOB _installed_cmake_files
  LIST_DIRECTORIES FALSE
  "${_config_dir}/*.cmake")
foreach(_installed_cmake_file IN LISTS _installed_cmake_files)
  file(READ "${_installed_cmake_file}" _installed_cmake_text)
  string(FIND "${_installed_cmake_text}" "${SOURCE_DIR}" _source_dir_ref_pos)
  if(NOT _source_dir_ref_pos EQUAL -1)
    message(FATAL_ERROR
      "Installed package export still references the producer source tree: ${SOURCE_DIR}\nFile: ${_installed_cmake_file}")
  endif()
endforeach()

message(STATUS "Configure consumer project...")
if(NOT EXISTS "${_consumer_source_dir}/CMakeLists.txt")
  message(FATAL_ERROR "Consumer project not found: '${_consumer_source_dir}'")
endif()

set(_consumer_cache_args ${_cmake_cache_args})
# Make dependencies installed into the same prefix (e.g., fmt) discoverable.
list(APPEND _consumer_cache_args "-DCMAKE_PREFIX_PATH=${_install_prefix}")
list(APPEND _consumer_cache_args "-DGENTEST_CONSUMER_LINK_MODE=${CONSUMER_LINK_MODE}")
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE)
  set(_installed_codegen_dir "${_install_prefix}/bin")
  file(MAKE_DIRECTORY "${_installed_codegen_dir}")
  set(_installed_codegen "${_installed_codegen_dir}/gentest_codegen${_exe_ext}")
  file(COPY_FILE "${PROG}" "${_installed_codegen}" ONLY_IF_DIFFERENT)
endif()
if(NOT _producer_fmt_dir STREQUAL "")
  # Keep consumer dependency resolution aligned with the producer package build.
  list(APPEND _consumer_cache_args "-Dfmt_DIR=${_producer_fmt_dir}")
endif()
if(CMAKE_HOST_WIN32 AND NOT _effective_cxx_compiler STREQUAL "" AND _effective_cxx_compiler MATCHES "[Cc]lang")
  # Keep the consumer build compatible with the producer's Windows+Clang settings.
  list(APPEND _consumer_cache_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
  list(APPEND _consumer_cache_args
       "-DCMAKE_CXX_FLAGS=-D_ITERATOR_DEBUG_LEVEL=0 -D_HAS_ITERATOR_DEBUGGING=0")
endif()

set(_consumer_dir_var "${PACKAGE_NAME}_DIR")
run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_generator_args}
    -S "${_consumer_source_dir}"
    -B "${_consumer_build_dir}"
    ${_consumer_cache_args}
    "-D${_consumer_dir_var}=${_config_dir}")
unset(_consumer_cache_args)

message(STATUS "Build consumer project...")
set(_consumer_build_args --build "${_consumer_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  list(APPEND _consumer_build_args --config "${BUILD_CONFIG}")
endif()
run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_consumer_build_args})

set(_consumer_exe "${_consumer_build_dir}/gentest_consumer${_exe_ext}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  set(_consumer_exe "${_consumer_build_dir}/${BUILD_CONFIG}/gentest_consumer${_exe_ext}")
endif()
if(NOT EXISTS "${_consumer_exe}")
  message(FATAL_ERROR "Consumer executable not found: '${_consumer_exe}'")
endif()

message(STATUS "Run consumer executable...")
run_or_fail(COMMAND "${_consumer_exe}")

message(STATUS "Run consumer list output...")
run_or_fail(COMMAND "${_consumer_exe}" --list)

message(STATUS "Run consumer module mock case...")
run_or_fail(COMMAND "${_consumer_exe}" --run=consumer/module_mock)

message(STATUS "Run consumer bench...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=bench --run=consumer/module_bench)

message(STATUS "Run consumer jitter...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=jitter --run=consumer/module_jitter)
