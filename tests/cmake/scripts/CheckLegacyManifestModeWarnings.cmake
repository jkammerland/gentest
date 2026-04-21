if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_legacy_warning "--output selects legacy manifest/single-TU mode")
set(_cmake_legacy_warning "OUTPUT selects legacy")

function(_gentest_expect_success label)
  set(one_value_args CONTAINS FORBIDS)
  set(multi_value_args COMMAND)
  cmake_parse_arguments(EXPECT "" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(NOT EXPECT_COMMAND)
    message(FATAL_ERROR "_gentest_expect_success requires COMMAND")
  endif()

  execute_process(
    COMMAND ${EXPECT_COMMAND}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_combined "${_out}\n${_err}")
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "${label}: expected success, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  if(DEFINED EXPECT_CONTAINS)
    string(FIND "${_combined}" "${EXPECT_CONTAINS}" _required_pos)
    if(_required_pos EQUAL -1)
      message(FATAL_ERROR
        "${label}: expected to find '${EXPECT_CONTAINS}'.\n"
        "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif()
  endif()

  if(DEFINED EXPECT_FORBIDS)
    string(FIND "${_combined}" "${EXPECT_FORBIDS}" _forbidden_pos)
    if(NOT _forbidden_pos EQUAL -1)
      message(FATAL_ERROR
        "${label}: did not expect to find '${EXPECT_FORBIDS}'.\n"
        "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif()
  endif()
endfunction()

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

set(_smoke_source "${SOURCE_DIR}/tests/smoke/codegen_axis_generators.cpp")
if(NOT EXISTS "${_smoke_source}")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: missing smoke source '${_smoke_source}'")
endif()

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${SOURCE_DIR}"
  INCLUDE_TESTS
  APPLE_SYSROOT)
gentest_normalize_std_flag_for_compiler(_codegen_std "clang++" "${CODEGEN_STD}")
list(APPEND _clang_args "${_codegen_std}" ${_public_include_args})

_gentest_expect_success(
  "cli legacy manifest output warning"
  CONTAINS "${_legacy_warning}"
  COMMAND
    "${PROG}"
    --output "${BUILD_ROOT}/cli_legacy_manifest.cpp"
    --compdb "${_compdb_root}"
    "${_smoke_source}"
    --
    ${_clang_args})

_gentest_expect_success(
  "cli per-tu output has no legacy manifest warning"
  FORBIDS "${_legacy_warning}"
  COMMAND
    "${PROG}"
    --check
    --tu-out-dir "${BUILD_ROOT}/tu-mode"
    --compdb "${_compdb_root}"
    "${_smoke_source}"
    --
    ${_clang_args})

function(_gentest_write_manifest_warning_fixture source_dir target_name test_name mode)
  file(REMOVE_RECURSE "${source_dir}")
  file(MAKE_DIRECTORY "${source_dir}")
  file(WRITE "${source_dir}/cases.cpp"
    "#include \"gentest/assertions.h\"\n"
    "\n"
    "[[using gentest: test(\"${test_name}/builds\")]]\n"
    "void ${target_name}_builds() {\n"
    "    gentest::asserts::EXPECT_TRUE(true);\n"
    "}\n")

  if("${mode}" STREQUAL "LEGACY_OUTPUT")
    string(CONCAT _fixture_codegen_call
      "gentest_attach_codegen(${target_name}\n"
      "    OUTPUT \"\${CMAKE_CURRENT_BINARY_DIR}/${target_name}.gentest.cpp\"\n"
      "    SOURCES \"\${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp\"\n"
      "    CLANG_ARGS \${_gentest_fixture_clang_args})\n")
  elseif("${mode}" STREQUAL "PER_TU_OUTPUT_DIR")
    string(CONCAT _fixture_codegen_call
      "gentest_attach_codegen(${target_name}\n"
      "    OUTPUT_DIR \"\${CMAKE_CURRENT_BINARY_DIR}/generated\"\n"
      "    SOURCES \"\${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp\"\n"
      "    CLANG_ARGS \${_gentest_fixture_clang_args})\n")
  else()
    message(FATAL_ERROR "Unknown manifest warning fixture mode '${mode}'")
  endif()

  file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir_for_cmake)
  set(_fixture_clang_args_text "${_clang_args}")
  string(REPLACE "\\" "\\\\" _fixture_clang_args_text "${_fixture_clang_args_text}")
  string(REPLACE "\"" "\\\"" _fixture_clang_args_text "${_fixture_clang_args_text}")
  file(WRITE "${source_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.31)\n"
    "project(gentest_${target_name} LANGUAGES CXX)\n"
    "\n"
    "set(CMAKE_CXX_EXTENSIONS OFF)\n"
    "set(gentest_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n"
    "set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL \"\" FORCE)\n"
    "set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING \"\" FORCE)\n"
    "\n"
    "add_subdirectory(\"${_gentest_source_dir_for_cmake}\" gentest)\n"
    "\n"
    "set(_gentest_fixture_clang_args \"${_fixture_clang_args_text}\")\n"
    "\n"
    "add_executable(${target_name})\n"
    "target_link_libraries(${target_name} PRIVATE gentest::gentest_main)\n"
    "target_compile_features(${target_name} PRIVATE cxx_std_20)\n"
    "\n"
    "${_fixture_codegen_call}")
endfunction()

function(_gentest_build_manifest_warning_fixture build_dir target_name)
  set(_build_args --build "${build_dir}" --target "${target_name}")
  if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
    list(APPEND _build_args --config "${BUILD_CONFIG}")
  elseif(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
    list(APPEND _build_args --config "${BUILD_TYPE}")
  endif()

  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" ${_build_args}
    STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${BUILD_ROOT}")
endfunction()

set(_fixture_source_dir "${BUILD_ROOT}/cmake_legacy_manifest_warning_src")
set(_fixture_build_dir "${BUILD_ROOT}/cmake_legacy_manifest_warning_build")
file(REMOVE_RECURSE "${_fixture_build_dir}")
_gentest_write_manifest_warning_fixture(
  "${_fixture_source_dir}"
  legacy_manifest_warning
  legacy_manifest_warning
  LEGACY_OUTPUT)

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED CXX_COMPILER_CLANG_SCAN_DEPS AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${CXX_COMPILER_CLANG_SCAN_DEPS}")
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

_gentest_expect_success(
  "cmake legacy manifest output warning"
  CONTAINS "${_cmake_legacy_warning}"
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_fixture_source_dir}"
    -B "${_fixture_build_dir}"
    ${_cmake_cache_args})
_gentest_build_manifest_warning_fixture("${_fixture_build_dir}" legacy_manifest_warning)

set(_per_tu_source_dir "${BUILD_ROOT}/cmake_per_tu_warning_probe_src")
set(_per_tu_build_dir "${BUILD_ROOT}/cmake_per_tu_warning_probe_build")
file(REMOVE_RECURSE "${_per_tu_build_dir}")
_gentest_write_manifest_warning_fixture(
  "${_per_tu_source_dir}"
  per_tu_warning_probe
  per_tu_warning_probe
  PER_TU_OUTPUT_DIR)

_gentest_expect_success(
  "cmake per-tu output has no legacy manifest warning"
  FORBIDS "${_cmake_legacy_warning}"
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_per_tu_source_dir}"
    -B "${_per_tu_build_dir}"
    ${_cmake_cache_args})
_gentest_build_manifest_warning_fixture("${_per_tu_build_dir}" per_tu_warning_probe)
