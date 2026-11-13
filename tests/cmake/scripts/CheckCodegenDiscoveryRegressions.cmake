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

# Needles must not contain ';' (a CMake list separator).
function(_gentest_cdr_expect_count what needle expected_count)
  string(REPLACE "${needle}" "" _without "${_registration}")
  string(LENGTH "${_registration}" _len_with)
  string(LENGTH "${_without}" _len_without)
  string(LENGTH "${needle}" _len_needle)
  math(EXPR _count "(${_len_with} - ${_len_without}) / ${_len_needle}")
  if(NOT _count EQUAL expected_count)
    message(FATAL_ERROR
      "${what}: expected ${expected_count} occurrence(s) of '${needle}', found ${_count}.\n${_log}\nRegistration:\n${_registration}")
  endif()
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

elseif(SCENARIO STREQUAL "attribute_text_scan")
  # Attribute discovery must not depend on text that only looks like a literal
  # or a comment, nor on specifiers written between attributes and a member.
  _gentest_cdr_scan(text CONTENT [=[
#pragma once
#if 0
See the users' guide.
An "unterminated quote.
#endif
// A line comment with one " quote and an apostrophe: don't
namespace scan {
[[using gentest: test("after_skipped_text")]] inline void after_skipped_text() {}
/* see http://example.com */ [[using gentest: test("after_block_comment")]] inline void after_block_comment() {}
[[using gentest: test("comment_before_decl")]] /* https://example.com/x */ // trailing
inline void comment_before_decl() {}
[[using gentest: test("subscript"), parameters(value, kTable[kIndex[0]])]] inline void subscript(int value) { (void)value; }
struct Members {
    [[using gentest: test("inline_member")]] inline void inline_member() {}
    [[using gentest: test("plain_member")]] void plain_member() {}
    [[using gentest: test("static_member")]] static inline void static_member() {}
    [[using gentest: test("const_ref_member")]] const int &const_ref_member() { return value; }
    [[nodiscard]] [[using gentest: test("constexpr_member")]] constexpr inline int constexpr_member() const { return value; }
    void unannotated() {}
    int value = 0;
};
inline void unannotated_free() {}
} // namespace scan
]=])
  _gentest_cdr_expect_success("attribute text scan")
  _gentest_cdr_expect_registered("attribute text scan"
    "\"scan/after_skipped_text\"" "\"scan/after_block_comment\"" "\"scan/comment_before_decl\""
    "\"scan/subscript(kTable[kIndex[0]])\"" "\"scan/inline_member\"" "\"scan/plain_member\"" "\"scan/static_member\""
    "\"scan/const_ref_member\"" "\"scan/constexpr_member\"")
  _gentest_cdr_expect_not_registered("attribute text scan" "unannotated")
elseif(SCENARIO STREQUAL "global_fixture_type")
  # A fixture type in the global namespace keeps its canonical spelling and is
  # never bound to a same-named type declared elsewhere.
  _gentest_cdr_scan(global CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
struct GFx {};
namespace other {
struct [[using gentest: fixture(suite)]] GFx {};
} // namespace other
namespace ns {
struct LFx {};
[[using gentest: test("global_local")]] inline void global_local(GFx &fx) { (void)fx; }
[[using gentest: test("ns_local")]] inline void ns_local(LFx &fx) { (void)fx; }
namespace inner {
struct GFx {};
[[using gentest: test("inner_global")]] inline void inner_global(::GFx &fx) { (void)fx; }
[[using gentest: test("inner_own")]] inline void inner_own(GFx &fx) { (void)fx; }
} // namespace inner
} // namespace ns
namespace other::deep {
[[using gentest: test("other_global")]] inline void other_global(::GFx &fx) { (void)fx; }
[[using gentest: test("other_shared")]] inline void other_shared(other::GFx &fx) { (void)fx; }
} // namespace other::deep
]=])
  _gentest_cdr_expect_success("global fixture type")
  _gentest_cdr_expect_not_registered("global fixture type" "FixtureHandle<ns::GFx>" "FixtureHandle<other::deep::GFx>")
  _gentest_cdr_expect_count("global fixture type" "auto fx0_ = ::gentest::detail::FixtureHandle<GFx>::empty()" 3)
  _gentest_cdr_expect_count("global fixture type" "auto fx0_ = ::gentest::detail::FixtureHandle<ns::LFx>::empty()" 1)
  _gentest_cdr_expect_count("global fixture type" "auto fx0_ = ::gentest::detail::FixtureHandle<ns::inner::GFx>::empty()" 1)
  _gentest_cdr_expect_registered("global fixture type" "\"other::GFx\")) return")
else()
  message(FATAL_ERROR "Unknown SCENARIO '${SCENARIO}'")
endif()
