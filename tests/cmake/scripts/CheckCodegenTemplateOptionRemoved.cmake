if(WIN32)
  message(STATUS "CheckCodegenTemplateOptionRemoved.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTemplateOptionRemoved.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTemplateOptionRemoved.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTemplateOptionRemoved.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CXX_COMPILER OR "${CXX_COMPILER}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTemplateOptionRemoved.cmake: CXX_COMPILER not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/template_option_removed")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_source "${_work_dir}/legacy_template_case.cpp")
set(_template "${_work_dir}/legacy_test_impl.cpp.tpl")

gentest_fixture_write_file("${_source}" [=[
#include "gentest/attributes.h"
#include "gentest/runner.h"

[[using gentest: test("legacy/template_wrapper_impls_only")]]
void legacy_template_case() {}
]=])

gentest_fixture_write_file("${_template}" [=[
// Legacy external template shape that still relies on {{WRAPPER_IMPLS}} via {{REGISTRATION_COMMON}}.
#include <array>
#include <span>
#include <type_traits>

#include "gentest/runner.h"
#include "gentest/fixture.h"

{{INCLUDE_SOURCES}}
{{WRAPPER_SUPPORT_COMMON}}
{{REGISTRATION_COMMON}}

constexpr std::array<gentest::Case, {{CASE_COUNT}}> kCases = {
{{CASE_INITS}}
};

struct GentestRegistrar {
    GentestRegistrar() {
{{FIXTURE_REGISTRATIONS}}        gentest::detail::register_cases(std::span{kCases});
    }
};

[[maybe_unused]] const GentestRegistrar kGentestRegistrar{};
]=])

file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
file(TO_CMAKE_PATH "${_source}" _source_norm)
gentest_make_public_api_compile_args(
  _compile_args
  COMPILER "${CXX_COMPILER}"
  STD "-std=c++20"
  SOURCE_ROOT "${_source_dir_norm}"
  EXTRA_ARGS
    "-c"
    "${_source_norm}"
    "-o"
    "legacy_template_case.o")

gentest_fixture_make_compdb_entry(
  _compdb_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_source_norm}"
  ARGUMENTS ${_compile_args})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_compdb_entry}")

execute_process(
  COMMAND
    "${PROG}"
    --compdb "${_work_dir}"
    --template "${_template}"
    --tu-out-dir "${_work_dir}/generated"
    "${_source}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "template option removal regression: gentest_codegen should reject removed manifest-mode template emission.
"
    "Output:
${_out}
Errors:
${_err}")
endif()

set(_all_output "${_out}
${_err}")
if(NOT _all_output MATCHES "--template was removed with legacy manifest/single-TU mode")
  message(FATAL_ERROR
    "template option removal regression: expected removed --template diagnostic.
"
    "Output:
${_all_output}")
endif()
