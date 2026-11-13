# Direct gentest_codegen invocations for discovery/render regressions. Each
# SCENARIO scans one fallback-header slot and checks the tool's exit status,
# its diagnostics and the generated registration source.
foreach(_required IN ITEMS BUILD_ROOT GENTEST_SOURCE_DIR PROG SCENARIO)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckCodegenDiscoveryRegressions.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clangxx)
  gentest_skip_test("codegen discovery regressions: host Clang not found")
  return()
endif()

set(_root "${BUILD_ROOT}/${SCENARIO}")
file(REMOVE_RECURSE "${_root}")

# _gentest_cdr_scan(<case> CONTENT <header text> [HEADER <relative path>]
#   [FLAGS <compile-command flags>...] [CODEGEN_ARGS <tool options>...]
#   [CLANG_ARGS <arguments after -->...])
# Sets _rc, _log (stdout + stderr) and _registration in the caller's scope.
function(_gentest_cdr_scan case_name)
  set(one_value_args CONTENT HEADER)
  set(multi_value_args FLAGS CODEGEN_ARGS CLANG_ARGS)
  cmake_parse_arguments(PARSE_ARGV 1 CDR "" "${one_value_args}" "${multi_value_args}")
  if(NOT CDR_HEADER)
    set(CDR_HEADER "cases.hpp")
  endif()

  set(_work "${_root}/${case_name}")
  set(_header "${_work}/${CDR_HEADER}")
  set(_registration_path "${_work}/generated/registration.cpp")
  get_filename_component(_header_dir "${_header}" DIRECTORY)
  file(MAKE_DIRECTORY "${_work}/generated" "${_header_dir}")
  gentest_fixture_write_file("${_header}" "${CDR_CONTENT}")
  gentest_fixture_make_compdb_entry(_entry
    DIRECTORY "${_work}" FILE "${_header}"
    ARGUMENTS "${_clangxx}" -std=c++20 ${CDR_FLAGS} -c "${_header}")
  gentest_fixture_write_compdb("${_work}/compile_commands.json" "${_entry}")

  set(_clang_args)
  if(CDR_CLANG_ARGS)
    set(_clang_args -- ${CDR_CLANG_ARGS})
  endif()
  execute_process(
    COMMAND "${PROG}" --compdb "${_work}" --host-clang "${_clangxx}"
      --tu-out-dir "${_work}/generated"
      --textual-registration-output "${_registration_path}"
      --artifact-manifest "${_work}/generated/manifest.json"
      --scan-slot-kind fallback-header ${CDR_CODEGEN_ARGS} "${_header}" ${_clang_args}
    WORKING_DIRECTORY "${_work}"
    RESULT_VARIABLE _scan_rc OUTPUT_VARIABLE _scan_out ERROR_VARIABLE _scan_err)
  set(_scan_registration "")
  if(EXISTS "${_registration_path}")
    file(READ "${_registration_path}" _scan_registration)
  endif()
  set(_rc "${_scan_rc}" PARENT_SCOPE)
  set(_log "${_scan_out}\n${_scan_err}" PARENT_SCOPE)
  set(_registration "${_scan_registration}" PARENT_SCOPE)
endfunction()

function(_gentest_cdr_expect_success what)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${what}: gentest_codegen failed unexpectedly (rc=${_rc}).\n${_log}")
  endif()
endfunction()

function(_gentest_cdr_expect_failure what expected_substring)
  if(_rc EQUAL 0)
    message(FATAL_ERROR "${what}: gentest_codegen must fail, but it exited 0.\n${_log}\nRegistration:\n${_registration}")
  endif()
  string(FIND "${_log}" "${expected_substring}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${what}: expected diagnostic '${expected_substring}' not found.\n${_log}")
  endif()
endfunction()

function(_gentest_cdr_expect_registered what)
  foreach(_needle IN LISTS ARGN)
    string(FIND "${_registration}" "${_needle}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "${what}: generated registration lacks '${_needle}'.\n${_log}\nRegistration:\n${_registration}")
    endif()
  endforeach()
endfunction()

function(_gentest_cdr_expect_not_registered what)
  foreach(_needle IN LISTS ARGN)
    string(FIND "${_registration}" "${_needle}" _pos)
    if(NOT _pos EQUAL -1)
      message(FATAL_ERROR "${what}: generated registration must not contain '${_needle}'.\n${_log}\nRegistration:\n${_registration}")
    endif()
  endforeach()
endfunction()

if(SCENARIO STREQUAL "quiet_clang_parse_error")
  # --quiet-clang hides warnings, never a translation unit that failed to parse.
  set(_content [=[
#pragma once
#error gentest quiet parse failure
namespace quiet {
[[using gentest: test("after_error")]] inline void after_error() {}
} // namespace quiet
]=])
  _gentest_cdr_scan(quiet CONTENT "${_content}" CODEGEN_ARGS --quiet-clang)
  _gentest_cdr_expect_failure("quiet parse error" "gentest quiet parse failure")
  _gentest_cdr_scan(loud CONTENT "${_content}")
  _gentest_cdr_expect_failure("loud parse error" "gentest quiet parse failure")

  set(_content [=[
#pragma once
namespace quiet {
[[using gentest: test("warned")]] inline void warned() {}
} // namespace quiet
]=])
  _gentest_cdr_scan(quiet_ok CONTENT "${_content}" CODEGEN_ARGS --quiet-clang)
  _gentest_cdr_expect_success("quiet clean scan")
  _gentest_cdr_expect_registered("quiet clean scan" "\"quiet/warned\"")
  if(_log MATCHES "warning:")
    message(FATAL_ERROR "--quiet-clang must still suppress warnings:\n${_log}")
  endif()
  _gentest_cdr_scan(loud_ok CONTENT "${_content}")
  _gentest_cdr_expect_success("loud clean scan")
  if(NOT _log MATCHES "unknown attribute")
    message(FATAL_ERROR "Expected the unsuppressed scan to report the unknown-attribute warning:\n${_log}")
  endif()
elseif(SCENARIO STREQUAL "shell_operator_arguments")
  # Shell operators inside one argument are ordinary text. Only a whole-token
  # operator ends a compile command (covered by CheckCodegenModuleCommandRetargeting).
  set(_content [=[
#pragma once
#if !(EXPR) || !defined(LATER_DEFINE) || !defined(EXTRA_LATER)
#error a flag after an argument containing a shell operator was dropped
#endif
namespace shell {
inline const char *greeting = GREETING;
[[using gentest: test("kept")]] inline void kept() {}
} // namespace shell
]=])
  _gentest_cdr_scan(embedded CONTENT "${_content}"
    FLAGS "-DEXPR=(1 || 0)" "-DPIPE=(1 | 2)" "-DGREETING=\"a && b | c\"" -DLATER_DEFINE=1
    CLANG_ARGS "-DEXTRA_EXPR=(0 || 1)" -DEXTRA_LATER=1)
  _gentest_cdr_expect_success("embedded shell operators")
  _gentest_cdr_expect_registered("embedded shell operators" "\"shell/kept\"")

else()
  message(FATAL_ERROR "Unknown SCENARIO '${SCENARIO}'")
endif()
