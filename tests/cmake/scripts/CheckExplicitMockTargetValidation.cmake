# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetValidation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetValidation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetValidation.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("explicit mock target validation regression: explicit mock targets currently require a single-config generator")
  return()
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("explicit mock target validation regression: no usable clang/clang++ pair was provided")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("explicit mock target validation regression: ${_supported_ninja_reason}")
    return()
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
gentest_append_host_apple_sysroot(_cmake_cache_args)

function(_gentest_expect_configure_failure test_case expected_substring)
  if(CMAKE_HOST_WIN32)
    set(_work_dir "${BUILD_ROOT}/${test_case}_emts")
  else()
    set(_work_dir "${BUILD_ROOT}/${test_case}")
  endif()
  set(_src_dir "${_work_dir}/src")
  set(_build_dir "${_work_dir}/build")
  file(REMOVE_RECURSE "${_work_dir}")
  file(MAKE_DIRECTORY "${_work_dir}")
  file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_src_dir}"
      -B "${_build_dir}"
      ${_cmake_cache_args}
      "-DTEST_CASE=${test_case}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected configure failure for TEST_CASE='${test_case}', but configure succeeded.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  set(_all "${_out}\n${_err}")
  string(FIND "${_all}" "${expected_substring}" _match_pos)
  if(_match_pos EQUAL -1)
    message(FATAL_ERROR
      "Configure for TEST_CASE='${test_case}' failed, but missing expected substring '${expected_substring}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

function(_gentest_expect_build_failure test_case expected_substring)
  if(CMAKE_HOST_WIN32)
    set(_work_dir "${BUILD_ROOT}/${test_case}_emts")
  else()
    set(_work_dir "${BUILD_ROOT}/${test_case}")
  endif()
  set(_src_dir "${_work_dir}/src")
  set(_build_dir "${_work_dir}/build")
  file(REMOVE_RECURSE "${_work_dir}")
  file(MAKE_DIRECTORY "${_work_dir}")
  file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_src_dir}"
      -B "${_build_dir}"
      ${_cmake_cache_args}
      "-DTEST_CASE=${test_case}"
    RESULT_VARIABLE _configure_rc
    OUTPUT_VARIABLE _configure_out
    ERROR_VARIABLE _configure_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _configure_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected configure success for TEST_CASE='${test_case}', but configure failed.\n"
      "--- stdout ---\n${_configure_out}\n--- stderr ---\n${_configure_err}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(_build_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected build failure for TEST_CASE='${test_case}', but build succeeded.\n"
      "--- stdout ---\n${_build_out}\n--- stderr ---\n${_build_err}")
  endif()

  set(_all "${_build_out}\n${_build_err}")
  string(FIND "${_all}" "${expected_substring}" _match_pos)
  if(_match_pos EQUAL -1)
    message(FATAL_ERROR
      "Build for TEST_CASE='${test_case}' failed, but missing expected substring '${expected_substring}'.\n"
      "--- stdout ---\n${_build_out}\n--- stderr ---\n${_build_err}")
  endif()
endfunction()

function(_gentest_expect_configure_success test_case out_build_dir)
  if(CMAKE_HOST_WIN32)
    set(_work_dir "${BUILD_ROOT}/${test_case}_emts")
  else()
    set(_work_dir "${BUILD_ROOT}/${test_case}")
  endif()
  set(_src_dir "${_work_dir}/src")
  set(_build_dir "${_work_dir}/build")
  file(REMOVE_RECURSE "${_work_dir}")
  file(MAKE_DIRECTORY "${_work_dir}")
  file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_src_dir}"
      -B "${_build_dir}"
      ${_cmake_cache_args}
      "-DTEST_CASE=${test_case}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "Expected configure success for TEST_CASE='${test_case}', but configure failed.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  set(${out_build_dir} "${_build_dir}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_build_success build_dir target_name)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --target "${target_name}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _build_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected build success for target '${target_name}', but build failed.\n"
      "--- stdout ---\n${_build_out}\n--- stderr ---\n${_build_err}")
  endif()
endfunction()

function(_gentest_expect_file_contains file expected_substring)
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "Expected file does not exist: ${file}")
  endif()
  file(READ "${file}" _content)
  string(FIND "${_content}" "${expected_substring}" _match_pos)
  if(_match_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected file '${file}' to contain '${expected_substring}'.\n"
      "--- content ---\n${_content}")
  endif()
endfunction()

function(_gentest_expect_file_excludes file forbidden_substring)
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "Expected file does not exist: ${file}")
  endif()
  file(READ "${file}" _content)
  string(FIND "${_content}" "${forbidden_substring}" _match_pos)
  if(NOT _match_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected file '${file}' to exclude '${forbidden_substring}'.\n"
      "--- content ---\n${_content}")
  endif()
endfunction()

function(_gentest_expect_single_glob out_file pattern)
  file(GLOB _matches "${pattern}")
  list(LENGTH _matches _match_count)
  if(NOT _match_count EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one file matching '${pattern}', got ${_match_count}.\n"
      "Matches: ${_matches}")
  endif()
  list(GET _matches 0 _match)
  set(${out_file} "${_match}" PARENT_SCOPE)
endfunction()

_gentest_expect_configure_failure("duplicate_output_dir" "Each explicit mock target must")
_gentest_expect_configure_failure("textual_module_name" "MODULE_NAME is not")
_gentest_expect_configure_failure("module_header_name" "HEADER_NAME is only")
_gentest_expect_configure_failure("mixed_surface_defs" "mixed textual and module")
_gentest_expect_configure_failure("absolute_header_name" "Absolute HEADER_NAME values")
_gentest_expect_configure_failure("escaped_header_name" "stay within OUTPUT_DIR")
_gentest_expect_configure_failure("reserved_header_name" "reserved generated output")
_gentest_expect_configure_failure("generator_expression_defs" "generator-expression")
_gentest_expect_configure_failure("generator_expression_output_dir" "OUTPUT_DIR")
_gentest_expect_configure_failure("unsupported_include_root_genex" "BUILD_INTERFACE/INSTALL_INTERFACE")
_gentest_expect_configure_failure("invalid_mock_backend" "BACKEND must be")
_gentest_expect_configure_failure("third_party_module_backend" "supports textual DEFS only")
_gentest_expect_configure_success("third_party_textual_backend_surface" _third_party_textual_backend_build_dir)
set(_third_party_public_header "${_third_party_textual_backend_build_dir}/generated/public/fixture_validation.hpp")
_gentest_expect_file_excludes("${_third_party_public_header}" "#define GENTEST_NO_AUTO_MOCK_INCLUDE 1")
_gentest_expect_file_excludes("${_third_party_public_header}" "#define GENTEST_NO_EXPECT_CALL_MACROS 1")
_gentest_expect_file_excludes("${_third_party_public_header}" "#include \"gentest/mock_fwd.h\"")
_gentest_expect_file_excludes("${_third_party_public_header}" "#include \"gentest/mock.h\"")
_gentest_expect_file_excludes("${_third_party_public_header}" "#undef GENTEST_NO_EXPECT_CALL_MACROS")
_gentest_expect_file_excludes("${_third_party_public_header}" "#undef GENTEST_NO_AUTO_MOCK_INCLUDE")
_gentest_expect_build_success("${_third_party_textual_backend_build_dir}" "explicit_validation_third_party_consumer")
set(_third_party_domain_header
  "${_third_party_textual_backend_build_dir}/generated/explicit_validation_third_party_textual_backend_mock_registry__domain_0000_header.hpp")
_gentest_expect_file_contains("${_third_party_domain_header}" "namespace fixture {\nnamespace validation {\nnamespace mocks {\n")
_gentest_expect_file_contains("${_third_party_domain_header}"
  "struct ThirdPartyServiceMock final : public ::fixture::validation::ThirdPartyService")
_gentest_expect_file_excludes("${_third_party_domain_header}" "#include \"gentest/mock_fwd.h\"")
_gentest_expect_file_excludes("${_third_party_domain_header}" "#include \"gentest/mock.h\"")
_gentest_expect_file_excludes("${_third_party_domain_header}" "namespace gentest")
_gentest_expect_file_excludes("${_third_party_domain_header}" "struct mock<")
_gentest_expect_configure_success("third_party_defs_include_mock_h" _third_party_defs_include_mock_h_build_dir)
_gentest_expect_build_success("${_third_party_defs_include_mock_h_build_dir}" "explicit_validation_third_party_consumer")
_gentest_expect_single_glob(_third_party_rewritten_defs
  "${_third_party_defs_include_mock_h_build_dir}/generated/defs/*_third_party_defs_include_mock_h.hpp")
_gentest_expect_file_excludes("${_third_party_rewritten_defs}" "#include \"gentest/mock.h\"")
_gentest_expect_file_contains("${_third_party_rewritten_defs}" "#include \"gentest/mock_fwd.h\"")
_gentest_expect_build_failure("third_party_same_file_defs_rejected"
  "third-party mock targets must be declared in a header separate from the mocked target definition")
_gentest_expect_build_failure("third_party_nested_target_rejected"
  "gmock mock backend does not support nested target types")
_gentest_expect_build_failure("third_party_template_target_rejected"
  "gmock mock backend does not support template-specialized target types")
_gentest_expect_build_failure("third_party_variadic_method_rejected"
  "gmock mock backend does not support C-style variadic methods")
_gentest_expect_build_failure("third_party_conversion_operator_rejected"
  "gmock mock backend does not support operator mocks")
_gentest_expect_build_failure("missing_named_module" "is not a named module source")
_gentest_expect_build_failure("provider_only_module" "has no named-module mocks to re-export")
_gentest_expect_build_failure("implementation_unit_module" "module implementation unit")
_gentest_expect_build_failure("partition_module" "declares module partition")
_gentest_expect_configure_failure("missing_module_name" "MODULE_NAME is")
_gentest_expect_build_failure("nested_module_class_scope" "named-module mock targets must be declared at namespace scope")
