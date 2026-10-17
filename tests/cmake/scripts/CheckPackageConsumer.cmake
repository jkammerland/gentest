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
#     [-DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=<path>]
#     [-DCONSUMER_LINK_MODE=<main_only|runtime_only|double>]
#     [-DPACKAGE_TEST_USE_MODULES=<ON|OFF>]
#     [-DPACKAGE_TEST_BUILD_EXAMPLES=<ON|OFF>]
#     [-DPACKAGE_TEST_EXPECT_MODULES=<AUTO|ON|OFF>]
#     [-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_WORK_DIR=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_PRODUCER_DIR=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_CONSUMER_EXE=<ON|OFF>]
#     [-DBUILD_TYPE=<Debug|Release|...>]
#     [-DBUILD_CONFIG=<Debug|Release|...>]   # for multi-config generators
#     [-DPACKAGE_TEST_PHASE=<SETUP|CONSUMER|CLEANUP>]
#     [-DPACKAGE_PRODUCER_ROOT=<shared CTest fixture root>]
#     -P tests/cmake/scripts/CheckPackageConsumer.cmake

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
if(NOT DEFINED PACKAGE_TEST_USE_MODULES OR "${PACKAGE_TEST_USE_MODULES}" STREQUAL "")
  set(PACKAGE_TEST_USE_MODULES ON)
endif()
if(NOT DEFINED PACKAGE_TEST_EXPECT_MODULES OR "${PACKAGE_TEST_EXPECT_MODULES}" STREQUAL "")
  set(PACKAGE_TEST_EXPECT_MODULES AUTO)
endif()
string(TOUPPER "${PACKAGE_TEST_EXPECT_MODULES}" PACKAGE_TEST_EXPECT_MODULES)
if(NOT PACKAGE_TEST_EXPECT_MODULES MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "PACKAGE_TEST_EXPECT_MODULES must be AUTO, ON, or OFF")
endif()
if(NOT DEFINED PACKAGE_TEST_DRY_RUN_WORK_DIR OR "${PACKAGE_TEST_DRY_RUN_WORK_DIR}" STREQUAL "")
  set(PACKAGE_TEST_DRY_RUN_WORK_DIR OFF)
endif()
if(NOT DEFINED PACKAGE_TEST_DRY_RUN_PRODUCER_DIR OR "${PACKAGE_TEST_DRY_RUN_PRODUCER_DIR}" STREQUAL "")
  set(PACKAGE_TEST_DRY_RUN_PRODUCER_DIR OFF)
endif()
if(NOT DEFINED PACKAGE_TEST_DRY_RUN_CONSUMER_EXE OR "${PACKAGE_TEST_DRY_RUN_CONSUMER_EXE}" STREQUAL "")
  set(PACKAGE_TEST_DRY_RUN_CONSUMER_EXE OFF)
endif()
if(NOT DEFINED PROG)
  set(PROG "")
endif()
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE
   AND NOT PACKAGE_TEST_DRY_RUN_WORK_DIR
   AND NOT PACKAGE_TEST_DRY_RUN_PRODUCER_DIR
   AND NOT PACKAGE_TEST_DRY_RUN_CONSUMER_EXE
   AND "${PROG}" STREQUAL "")
  message(FATAL_ERROR
    "CheckPackageConsumer.cmake: PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON requires PROG to point at gentest_codegen")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
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

function(_gentest_read_configured_cxx_compiler_info build_dir out_found out_id out_version)
  file(GLOB _compiler_files LIST_DIRECTORIES FALSE "${build_dir}/CMakeFiles/*/CMakeCXXCompiler.cmake")
  if(NOT _compiler_files)
    set(${out_found} FALSE PARENT_SCOPE)
    set(${out_id} "" PARENT_SCOPE)
    set(${out_version} "" PARENT_SCOPE)
    return()
  endif()

  list(GET _compiler_files 0 _compiler_file)
  file(STRINGS "${_compiler_file}" _compiler_id_line REGEX "^set\\(CMAKE_CXX_COMPILER_ID \"")
  file(STRINGS "${_compiler_file}" _compiler_version_line REGEX "^set\\(CMAKE_CXX_COMPILER_VERSION \"")
  if(NOT _compiler_id_line OR NOT _compiler_version_line)
    set(${out_found} FALSE PARENT_SCOPE)
    set(${out_id} "" PARENT_SCOPE)
    set(${out_version} "" PARENT_SCOPE)
    return()
  endif()

  list(GET _compiler_id_line 0 _compiler_id_line_value)
  list(GET _compiler_version_line 0 _compiler_version_line_value)
  string(REGEX REPLACE "^set\\(CMAKE_CXX_COMPILER_ID \"([^\"]*)\"\\)$" "\\1" _compiler_id "${_compiler_id_line_value}")
  string(REGEX REPLACE "^set\\(CMAKE_CXX_COMPILER_VERSION \"([^\"]*)\"\\)$" "\\1" _compiler_version "${_compiler_version_line_value}")

  set(${out_found} TRUE PARENT_SCOPE)
  set(${out_id} "${_compiler_id}" PARENT_SCOPE)
  set(${out_version} "${_compiler_version}" PARENT_SCOPE)
endfunction()

set(_exe_ext "")
if(CMAKE_HOST_WIN32)
  set(_exe_ext ".exe")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/PackageFixtureContext.cmake")
gentest_package_fixture_context()

# Each consumer owns its scratch directory; only the producer prefix is shared.
string(SHA256 _work_hash "${_producer_hash}|${CONSUMER_LINK_MODE}|${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}")
string(SUBSTRING "${_work_hash}" 0 12 _work_hash_short)
set(_work_dir "${BUILD_ROOT}/consumer_${_work_hash_short}")
set(_consumer_build_dir "${_work_dir}/c")
set(_consumer_source_dir "${_work_dir}/src")

if(PACKAGE_TEST_DRY_RUN_WORK_DIR)
  message(STATUS "CheckPackageConsumer work dir: ${_work_dir}")
endif()
if(PACKAGE_TEST_DRY_RUN_PRODUCER_DIR)
  message(STATUS "CheckPackageConsumer producer dir: ${_producer_root}")
endif()
set(_dry_run_consumer_exe "${_consumer_build_dir}/gentest_consumer${_exe_ext}")
if(NOT "${_effective_build_config}" STREQUAL "")
  set(_dry_run_consumer_exe "${_consumer_build_dir}/${_effective_build_config}/gentest_consumer${_exe_ext}")
endif()
if(PACKAGE_TEST_DRY_RUN_CONSUMER_EXE)
  message(STATUS "CheckPackageConsumer consumer exe: ${_dry_run_consumer_exe}")
endif()
if(PACKAGE_TEST_DRY_RUN_WORK_DIR OR PACKAGE_TEST_DRY_RUN_PRODUCER_DIR OR PACKAGE_TEST_DRY_RUN_CONSUMER_EXE)
  return()
endif()

if(NOT DEFINED PACKAGE_TEST_PHASE)
  set(PACKAGE_TEST_PHASE CONSUMER)
endif()
if(PACKAGE_TEST_PHASE STREQUAL "CLEANUP")
  gentest_remove_fixture_path("${_producer_root}")
  return()
endif()

if(PACKAGE_TEST_PHASE STREQUAL "SETUP")
  # CTest owns freshness and lifetime. Every invocation starts from source;
  # there is no partial-source fingerprint, cached producer, or lock protocol.
  gentest_remove_fixture_path("${_producer_root}")
  file(MAKE_DIRECTORY "${_producer_root}")
  set(_work_dir "${_producer_root}")
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
      "-DFETCHCONTENT_BASE_DIR=${_fetchcontent_base_dir}"
      "-DCMAKE_INSTALL_PREFIX=${_install_prefix}")
  gentest_assert_windows_native_llvm_cache_args(
    "${_producer_build_dir}" "${_effective_cxx_compiler}" "package consumer producer")

  message(STATUS "Build and install producer into '${_install_prefix}'...")
  set(_producer_build_args --build "${_producer_build_dir}" --target install)
  if(NOT "${_effective_build_config}" STREQUAL "")
    list(APPEND _producer_build_args --config "${_effective_build_config}")
  endif()
  run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_producer_build_args})
  return()
elseif(NOT PACKAGE_TEST_PHASE STREQUAL "CONSUMER")
  message(FATAL_ERROR "Unknown package fixture phase: ${PACKAGE_TEST_PHASE}")
endif()

if(NOT EXISTS "${_producer_build_dir}/CMakeCache.txt" OR NOT IS_DIRECTORY "${_install_prefix}")
  message(FATAL_ERROR "Package producer fixture is unavailable: ${_producer_root}. Run through CTest or use PACKAGE_TEST_PHASE=SETUP first.")
endif()

if(PACKAGE_TEST_USE_MODULES)
  set(_producer_cache_file "${_producer_build_dir}/CMakeCache.txt")
  set(_effective_expect_modules "${PACKAGE_TEST_EXPECT_MODULES}")
  _gentest_read_configured_cxx_compiler_info("${_producer_build_dir}" _compiler_info_found _compiler_id_value _compiler_version_value)
  if(_compiler_info_found AND _compiler_id_value STREQUAL "GNU")
    string(REGEX MATCH "^[0-9]+" _compiler_major "${_compiler_version_value}")
    if(NOT _compiler_major STREQUAL "" AND _compiler_major LESS 15)
      gentest_skip_test("installed-package module consumer smoke unavailable because configured GNU compiler ${_compiler_version_value} is older than 15")
      return()
    endif()
    if(_effective_expect_modules STREQUAL "AUTO")
      set(_effective_expect_modules ON)
    endif()
  endif()
  gentest_read_cache_value("${_producer_cache_file}" "GENTEST_PUBLIC_MODULES_ENABLED"
    _modules_enabled_found _modules_enabled_value)
  if(_modules_enabled_found AND NOT _modules_enabled_value)
    gentest_read_cache_value("${_producer_cache_file}" "GENTEST_PUBLIC_MODULES_DISABLED_REASON"
      _modules_reason_found _modules_disabled_reason)
    if(NOT _modules_reason_found OR "${_modules_disabled_reason}" STREQUAL "")
      set(_modules_disabled_reason "module support was disabled during producer configure")
    endif()
    if(_effective_expect_modules STREQUAL "ON")
      message(FATAL_ERROR
        "installed-package module consumer smoke expected gentest public named modules for '${_effective_cxx_compiler}', but the producer disabled them: ${_modules_disabled_reason}")
    endif()
    gentest_skip_test("installed-package module consumer smoke unavailable because gentest public named modules are disabled for '${_effective_cxx_compiler}': ${_modules_disabled_reason}")
    return()
  endif()
endif()

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/tests/consumer/" DESTINATION "${_consumer_source_dir}")

set(_legacy_scan_inspector_dir "${_install_prefix}/share/cmake/gentest/scan_inspector")
if(EXISTS "${_legacy_scan_inspector_dir}")
  message(FATAL_ERROR
    "Installed package must not contain the legacy scan_inspector helper directory: '${_legacy_scan_inspector_dir}'")
endif()
set(_removed_codegen_inspector "${_install_prefix}/share/cmake/gentest/gentest/CodegenInspector.cmake")
if(EXISTS "${_removed_codegen_inspector}")
  message(FATAL_ERROR
    "Installed package must not contain removed configure-time source inspector helper: '${_removed_codegen_inspector}'")
endif()

set(_producer_fmt_dir "")
set(_producer_fmt_version "")
set(_producer_cache_file "${_producer_build_dir}/CMakeCache.txt")
if(EXISTS "${_producer_cache_file}")
  file(STRINGS "${_producer_cache_file}" _producer_fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
  if(_producer_fmt_dir_line)
    list(GET _producer_fmt_dir_line 0 _producer_fmt_dir_line_value)
    string(REGEX REPLACE "^fmt_DIR:PATH=" "" _producer_fmt_dir "${_producer_fmt_dir_line_value}")
    unset(_producer_fmt_dir_line_value)
  endif()
  gentest_read_cache_value("${_producer_cache_file}" "GENTEST_FMT_VERSION" _producer_fmt_version_found _producer_fmt_version)
  if(NOT _producer_fmt_version_found OR "${_producer_fmt_version}" STREQUAL "")
    message(FATAL_ERROR "Producer cache must record GENTEST_FMT_VERSION for installed package dependency export")
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
file(READ "${_config_file}" _installed_config_text)
set(_expected_fmt_dependency "find_dependency(fmt ${_producer_fmt_version} EXACT CONFIG REQUIRED)")
string(FIND "${_installed_config_text}" "${_expected_fmt_dependency}" _expected_fmt_dependency_pos)
if(_expected_fmt_dependency_pos EQUAL -1)
  message(FATAL_ERROR
    "Installed package config must require the producer's exact fmt version.\n"
    "Expected line: ${_expected_fmt_dependency}\n"
    "Config file: ${_config_file}")
endif()
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
list(APPEND _consumer_cache_args "-DGENTEST_CONSUMER_USE_MODULES=${PACKAGE_TEST_USE_MODULES}")
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE)
  # The shared installed prefix is immutable. Explicit codegen uses the
  # caller-owned host executable; native-codegen consumers use the export.
  list(APPEND _consumer_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
if(NOT _producer_fmt_dir STREQUAL "")
  # Keep consumer dependency resolution aligned with the producer package build.
  list(APPEND _consumer_cache_args "-Dfmt_DIR=${_producer_fmt_dir}")
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
gentest_assert_windows_native_llvm_cache_args(
  "${_consumer_build_dir}" "${_effective_cxx_compiler}" "package consumer consumer")

message(STATUS "Build consumer project...")
set(_consumer_build_args --build "${_consumer_build_dir}")
if(NOT "${_effective_build_config}" STREQUAL "")
  list(APPEND _consumer_build_args --config "${_effective_build_config}")
endif()
run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_consumer_build_args})

set(_consumer_exe "${_consumer_build_dir}/gentest_consumer${_exe_ext}")
if(NOT "${_effective_build_config}" STREQUAL "")
  set(_consumer_exe "${_consumer_build_dir}/${_effective_build_config}/gentest_consumer${_exe_ext}")
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

message(STATUS "Run consumer log sink case...")
run_or_fail(COMMAND "${_consumer_exe}" --run=consumer/log_sink)

message(STATUS "Run consumer bench...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=bench --run=consumer/module_bench)

message(STATUS "Run consumer jitter...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=jitter --run=consumer/module_jitter)

if(PACKAGE_TEST_BUILD_EXAMPLES)
  include("${CMAKE_CURRENT_LIST_DIR}/CheckFeatureExamples.cmake")
endif()

# Consumers own their scratch output. CTest runs shared producer cleanup once
# after every dependent consumer has finished, including failed consumers.
gentest_remove_fixture_path("${_work_dir}")
