if(WIN32)
  message(STATUS "CheckCodegenLegacyWrapperImplsTemplateRejected.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenLegacyWrapperImplsTemplateRejected.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenLegacyWrapperImplsTemplateRejected.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenLegacyWrapperImplsTemplateRejected.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CXX_COMPILER OR "${CXX_COMPILER}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenLegacyWrapperImplsTemplateRejected.cmake: CXX_COMPILER not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/legacy_wrapper_impls_template_rejected")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_source "${_work_dir}/legacy_template_case.cpp")
set(_template "${_work_dir}/legacy_test_impl.cpp.tpl")
set(_output "${_work_dir}/legacy_template_case.gentest.cpp")

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
set(_compile_args
  "${CXX_COMPILER}"
  "-std=c++20"
  "-I${_source_dir_norm}/include")
gentest_append_public_dependency_include_args(_compile_args)
list(APPEND _compile_args
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
    --output "${_output}"
    "${_source}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "legacy external template regression: gentest_codegen should reject templates that rely on legacy {{WRAPPER_IMPLS}} placement.
"
    "Output:
${_out}
Errors:
${_err}")
endif()

set(_all_output "${_out}
${_err}")
if(NOT _all_output MATCHES "GLOBAL_WRAPPER_IMPLS")
  message(FATAL_ERROR
    "legacy external template regression: expected migration diagnostic mentioning {{GLOBAL_WRAPPER_IMPLS}}.
"
    "Output:
${_all_output}")
endif()
