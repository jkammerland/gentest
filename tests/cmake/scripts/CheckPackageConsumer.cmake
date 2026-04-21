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
#     [-DPACKAGE_TEST_EXPECT_MODULES=<AUTO|ON|OFF>]
#     [-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_WORK_DIR=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_PRODUCER_DIR=<ON|OFF>]
#     [-DPACKAGE_TEST_DRY_RUN_CONSUMER_EXE=<ON|OFF>]
#     [-DBUILD_TYPE=<Debug|Release|...>]
#     [-DBUILD_CONFIG=<Debug|Release|...>]   # for multi-config generators
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

gentest_resolve_fixture_build_type(_effective_build_type "${_effective_cxx_compiler}" "${BUILD_TYPE}")
set(_effective_build_config "")
set(_is_multi_config_generator FALSE)
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  if(GENERATOR STREQUAL "Ninja Multi-Config" OR GENERATOR MATCHES "^Visual Studio" OR GENERATOR STREQUAL "Xcode")
    set(_is_multi_config_generator TRUE)
  endif()
endif()
if(_is_multi_config_generator)
  gentest_resolve_fixture_build_type(_effective_build_config "${_effective_cxx_compiler}" "${BUILD_CONFIG}")
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
if(DEFINED PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS
   AND NOT "${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  list(APPEND _cmake_cache_args
       "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_cmake_cache_args "${_effective_cxx_compiler}" ${_cmake_cache_args})
set(_windows_native_llvm_policy_key "")
gentest_is_windows_native_llvm_clang(_uses_windows_native_llvm_compat "${_effective_cxx_compiler}")
if(_uses_windows_native_llvm_compat)
  set(_windows_native_llvm_policy_key "win-native-llvm-cache-v1")
endif()
list(APPEND _cmake_cache_args "-DGENTEST_BUILD_CODEGEN=ON")
if(PACKAGE_TEST_USE_MODULES)
  list(APPEND _cmake_cache_args "-DGENTEST_ENABLE_PUBLIC_MODULES=AUTO")
endif()

set(_producer_surface_files
    "${SOURCE_DIR}/CMakeLists.txt"
    "${SOURCE_DIR}/src/CMakeLists.txt"
    "${SOURCE_DIR}/cmake/GentestDependencies.cmake"
    "${SOURCE_DIR}/cmake/GentestFmtDependency.cmake"
    "${SOURCE_DIR}/cmake/GentestCodegen.cmake"
    "${SOURCE_DIR}/third_party/target_install_package/VENDORED_TAG.txt")
file(GLOB_RECURSE _producer_codegen_cmake_modules
  LIST_DIRECTORIES FALSE
  "${SOURCE_DIR}/cmake/gentest/*.cmake")
list(APPEND _producer_surface_files ${_producer_codegen_cmake_modules})
file(GLOB_RECURSE _producer_public_headers
  LIST_DIRECTORIES FALSE
  "${SOURCE_DIR}/include/gentest/*.h"
  "${SOURCE_DIR}/include/gentest/*.hpp"
  "${SOURCE_DIR}/include/gentest/*.cppm")
list(APPEND _producer_surface_files ${_producer_public_headers})
list(REMOVE_DUPLICATES _producer_surface_files)
list(SORT _producer_surface_files)

set(_producer_source_surface_key "")
foreach(_producer_surface_file IN LISTS _producer_surface_files)
  if(EXISTS "${_producer_surface_file}")
    file(SHA256 "${_producer_surface_file}" _producer_surface_hash)
    string(APPEND _producer_source_surface_key "|${_producer_surface_file}:${_producer_surface_hash}")
  endif()
endforeach()

file(GLOB_RECURSE _tip_vendored_cmake_surface
  LIST_DIRECTORIES FALSE
  "${SOURCE_DIR}/third_party/target_install_package/share/cmake/target_install_package/*")
list(SORT _tip_vendored_cmake_surface)
foreach(_producer_surface_file IN LISTS _tip_vendored_cmake_surface)
  file(SHA256 "${_producer_surface_file}" _producer_surface_hash)
  string(APPEND _producer_source_surface_key "|${_producer_surface_file}:${_producer_surface_hash}")
endforeach()

set(_work_dir_semantic_key
  "${PACKAGE_NAME}|${CONSUMER_LINK_MODE}|${PACKAGE_TEST_USE_MODULES}|${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}|${_effective_c_compiler}|${_effective_cxx_compiler}|${_effective_build_type}|${BUILD_CONFIG}|${PROG}|${_windows_native_llvm_policy_key}|${_producer_source_surface_key}")
string(MD5 _work_dir_hash "${_work_dir_semantic_key}")
string(SUBSTRING "${_work_dir_hash}" 0 12 _work_dir_hash_short)
set(_producer_semantic_key
  "${PACKAGE_NAME}|${PACKAGE_TEST_USE_MODULES}|${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}|${_effective_c_compiler}|${_effective_cxx_compiler}|${_effective_build_type}|${BUILD_CONFIG}|${PROG}|${_windows_native_llvm_policy_key}|${_producer_source_surface_key}")
string(MD5 _producer_hash "${_producer_semantic_key}")
string(SUBSTRING "${_producer_hash}" 0 12 _producer_hash_short)
string(REGEX REPLACE "[^A-Za-z0-9]+" "_" _package_tag "${PACKAGE_NAME}")
string(TOLOWER "${_package_tag}" _package_tag)
if(_package_tag STREQUAL "")
  set(_package_tag "pkg")
endif()
set(_work_dir "${BUILD_ROOT}/pkg_${_package_tag}_${_work_dir_hash_short}")
set(_producer_root "${BUILD_ROOT}/pkg_${_package_tag}_producer_${_producer_hash_short}")
set(_producer_build_dir "${_producer_root}/p")
set(_install_prefix "${_producer_root}/i")
set(_consumer_build_dir "${_work_dir}/c")
set(_consumer_source_dir "${_work_dir}/src")
set(_fetchcontent_base_dir "${_producer_root}/fc")
set(_producer_lock "${_producer_root}/producer.lock")

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

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/tests/consumer/" DESTINATION "${_consumer_source_dir}")
file(MAKE_DIRECTORY "${_producer_root}")
file(LOCK "${_producer_lock}" GUARD PROCESS TIMEOUT 1800)

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

message(STATUS "Build and install producer into '${_install_prefix}'...")
if(_is_multi_config_generator)
  # Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) often
  # generate for several configurations at once. Install both Debug and Release
  # so imported targets have locations for common configs (and for the config
  # mapping logic in the generated *Config.cmake files).
  set(_install_configs Debug Release)
  if(NOT "${_effective_build_config}" STREQUAL "")
    list(APPEND _install_configs "${_effective_build_config}")
  endif()
  list(REMOVE_DUPLICATES _install_configs)
  foreach(_cfg IN LISTS _install_configs)
    run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install --config "${_cfg}")
  endforeach()
else()
  run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install)
endif()

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
file(LOCK "${_producer_lock}" RELEASE GUARD PROCESS)

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
  set(_installed_codegen_dir "${_install_prefix}/bin")
  file(MAKE_DIRECTORY "${_installed_codegen_dir}")
  set(_installed_codegen "${_installed_codegen_dir}/gentest_codegen${_exe_ext}")
  file(COPY_FILE "${PROG}" "${_installed_codegen}" ONLY_IF_DIFFERENT)
  list(APPEND _consumer_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${_installed_codegen}")
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

message(STATUS "Run consumer bench...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=bench --run=consumer/module_bench)

message(STATUS "Run consumer jitter...")
run_or_fail(COMMAND "${_consumer_exe}" --kind=jitter --run=consumer/module_jitter)

# Nested producer + consumer builds can occupy substantial disk space on
# module-enabled Debug lanes. Remove the scratch roots after success so later
# helper tests do not inherit a nearly full workspace.
gentest_remove_fixture_path("${_work_dir}")
gentest_remove_fixture_path("${_producer_root}")
