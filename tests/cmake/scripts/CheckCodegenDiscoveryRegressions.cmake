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
elseif(SCENARIO STREQUAL "numeric_axes")
  # Axis generators classify the parameter by its real type and parse whole
  # literals; anything else is an error instead of a silent 0.
  _gentest_cdr_scan(values CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
#include <cstddef>
namespace axis {
using Wide = long double;
[[using gentest: test("ld"), range(x, 0.0, 0.25, 1.0)]] inline void ld(Wide x) { (void)x; }
[[using gentest: test("hex"), range(i, 0x10, 1, 0x12)]] inline void hex(int i) { (void)i; }
[[using gentest: test("lin"), linspace(x, 0, 1, 1e3)]] inline void lin(double x) { (void)x; }
[[using gentest: test("big"), range(i, 9223372036854775805, 1, 9223372036854775807)]] inline void big(long long i) { (void)i; }
[[using gentest: test("sep"), range(n, 1'000u, 500, 2'000UL)]] inline void sep(const std::size_t &n) { (void)n; }
[[using gentest: test("down"), range(i, 1, -1, -1)]] inline void down(short i) { (void)i; }
[[using gentest: test("flt"), range(f, 2.5e-1f, .25, 7.5e-1)]] inline void flt(float f) { (void)f; }
[[using gentest: test("geo"), geom(i, 0b1, 2, 4)]] inline void geo(unsigned i) { (void)i; }
} // namespace axis
]=])
  _gentest_cdr_expect_success("numeric axes")
  _gentest_cdr_expect_registered("numeric axes"
    "\"axis/ld(0)\"" "\"axis/ld(0.25)\"" "\"axis/ld(0.5)\"" "\"axis/ld(0.75)\"" "\"axis/ld(1)\""
    "\"axis/hex(16)\"" "\"axis/hex(17)\"" "\"axis/hex(18)\""
    "\"axis/lin(0)\"" "\"axis/lin(1)\""
    "\"axis/big(9223372036854775805)\"" "\"axis/big(9223372036854775806)\"" "\"axis/big(9223372036854775807)\""
    "\"axis/sep(1000)\"" "\"axis/sep(1500)\"" "\"axis/sep(2000)\""
    "\"axis/down(1)\"" "\"axis/down(0)\"" "\"axis/down(-1)\""
    "\"axis/flt(0.25)\"" "\"axis/flt(0.5)\"" "\"axis/flt(0.75)\""
    "\"axis/geo(1)\"" "\"axis/geo(2)\"" "\"axis/geo(4)\"" "\"axis/geo(8)\"")
  _gentest_cdr_expect_count("numeric axes" "\"axis/ld(" 5)
  _gentest_cdr_expect_count("numeric axes" "\"axis/hex(" 3)
  _gentest_cdr_expect_count("numeric axes" "\"axis/lin(" 1000)
  _gentest_cdr_expect_count("numeric axes" "\"axis/big(" 3)

  _gentest_cdr_scan(named_constant CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace axis {
inline constexpr int kLast = 3;
[[using gentest: test("named"), range(i, 0, 1, kLast)]] inline void named(int i) { (void)i; }
} // namespace axis
]=])
  _gentest_cdr_expect_failure("named axis bound" "end 'kLast' is not an integer literal")

  _gentest_cdr_scan(partial_literal CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace axis {
[[using gentest: test("partial"), linspace(x, 0, 1.5.2, 3)]] inline void partial(double x) { (void)x; }
} // namespace axis
]=])
  _gentest_cdr_expect_failure("partially numeric axis bound" "end '1.5.2' is not a finite numeric literal")

  _gentest_cdr_scan(substring_type CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace axis {
struct Point {};
[[using gentest: test("substring"), range(p, 0, 1, 2)]] inline void substring(Point p) { (void)p; }
} // namespace axis
]=])
  _gentest_cdr_expect_failure("non-numeric axis parameter" "parameters_range not supported for parameter 'p' of type 'axis::Point'")
elseif(SCENARIO STREQUAL "string_literal_parameters")
  # String-literal parameter values reach the generated call verbatim.
  _gentest_cdr_scan(escapes CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
#include <string_view>
#include <utility>
namespace esc {
[[using gentest: test("str"), parameters(s, "a\x41", "nul\0x", "q\"uote\\")]] inline void str(std::string_view s) { (void)s; }
[[using gentest: test("wide"), parameters(s, "w\x41")]] inline void wide(std::wstring_view s) { (void)s; }
[[using gentest: test("chr"), parameters(c, "\n", "a")]] inline void chr(char c) { (void)c; }
[[using gentest: test("expr"), parameters(p, "std::pair<int, int>{1, 2}")]] inline void expr(std::pair<int, int> p) { (void)p; }
[[using gentest: test("name\x41\ttab")]] inline void named() {}
} // namespace esc
]=])
  _gentest_cdr_expect_success("string literal parameters")
  _gentest_cdr_expect_registered("string literal parameters"
    [=[esc::str("a\x41")]=] [=[esc::str("nul\0x")]=] [=[esc::str("q\"uote\\")]=]
    [=[esc::wide(L"w\x41")]=] [=[esc::chr('\n')]=] [=[esc::chr('a')]=]
    [=[esc::expr(std::pair<int, int>{1, 2})]=] [=["esc/nameA\ttab"]=])
  _gentest_cdr_expect_not_registered("string literal parameters" [=["ax41"]=] [=["nul0x"]=])
elseif(SCENARIO STREQUAL "template_fixture_bindings")
  # A fixture parameter typed by a bound template parameter is instantiated
  # with the bound type; unbound template parameters are diagnosed.
  _gentest_cdr_scan(bound CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace tpl {
struct FxA {};
struct [[using gentest: fixture(suite)]] FxB {};
namespace nested {
struct FxC {};
} // namespace nested
using Alias = nested::FxC;
template <typename T> [[using gentest: test("tfx"), template(T, FxA, tpl::FxB, Alias)]] void tfx(T &fx) { (void)fx; }
template <typename T> [[using gentest: test("deduced"), parameters(value, 1)]] void deduced(T value) { (void)value; }
template <typename T = int> [[using gentest: test("defaulted")]] void defaulted() {}
} // namespace tpl
]=])
  _gentest_cdr_expect_success("template fixture bindings")
  _gentest_cdr_expect_registered("template fixture bindings"
    "auto fx0_ = ::gentest::detail::FixtureHandle<tpl::FxA>::empty()"
    "auto fx0_ = ::gentest::detail::FixtureHandle<tpl::nested::FxC>::empty()"
    "::gentest::detail::SharedFixtureScope::Suite, \"tpl\", \"tpl::FxB\""
    "\"tpl/tfx<FxA>\"" "\"tpl/tfx<tpl::FxB>\"" "\"tpl/tfx<Alias>\"" "tpl::deduced<>(1)" "tpl::defaulted<>()")
  _gentest_cdr_expect_not_registered("template fixture bindings" "type-parameter")

  _gentest_cdr_scan(unbound CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace tpl {
template <typename T> [[using gentest: test("unbound")]] void unbound();
} // namespace tpl
]=])
  _gentest_cdr_expect_failure("unbound template parameter" "function template requires a 'template(T, ...)' attribute")

  _gentest_cdr_scan(dependent CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace tpl {
template <typename T> struct Box {};
template <typename T> [[using gentest: test("dependent"), template(T, int)]] void dependent(Box<T> &fx) { (void)fx; }
} // namespace tpl
]=])
  _gentest_cdr_expect_failure("dependent fixture parameter" "which depends on a template parameter")

  _gentest_cdr_scan(unresolved CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace tpl {
template <typename T> struct Box {};
template <typename T> [[using gentest: test("unresolved"), template(T, Box<int>)]] void unresolved(T &fx) { (void)fx; }
} // namespace tpl
]=])
  _gentest_cdr_expect_failure("unresolvable fixture binding" "cannot use template binding 'Box<int>' as the fixture type")
elseif(SCENARIO STREQUAL "display_name_uniqueness")
  # Registered names must be unique even when the base names differ.
  _gentest_cdr_scan(collision CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace dup {
[[using gentest: test("p"), parameters(i, 1, 2)]] inline void with_axis(int i) { (void)i; }
[[using gentest: test("p(1)")]] inline void spelled_out() {}
} // namespace dup
]=])
  _gentest_cdr_expect_failure("display name collision" "duplicate test name 'dup/p(1)'")

  _gentest_cdr_scan(distinct CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
namespace dup {
[[using gentest: test("p"), parameters(i, 1, 2)]] inline void with_axis(int i) { (void)i; }
[[using gentest: test("p(3)")]] inline void spelled_out() {}
} // namespace dup
]=])
  _gentest_cdr_expect_success("distinct display names")
  _gentest_cdr_expect_registered("distinct display names" "\"dup/p(1)\"" "\"dup/p(2)\"" "\"dup/p(3)\"")
elseif(SCENARIO STREQUAL "member_template_arguments")
  # Template arguments containing "::" are not part of the member name.
  _gentest_cdr_scan(member CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
#include <string>
#include <utility>
namespace memtpl {
struct Fixture {
    template <typename T>
    [[using gentest: test("typed"), template(T, std::string, std::pair<std::string, int>)]]
    void typed() {}
    [[using gentest: test("less"), parameters(rhs, 7)]]
    bool operator<(int rhs) { return rhs == 7; }
};
} // namespace memtpl
]=])
  _gentest_cdr_expect_success("member template arguments")
  _gentest_cdr_expect_registered("member template arguments"
    ".template typed<std::string>()" ".template typed<std::pair<std::string, int>>()" ".operator<(7)")
  _gentest_cdr_expect_not_registered("member template arguments" ".template string>" ".template pair<")
elseif(SCENARIO STREQUAL "discarded_results")
  # A discarded result is never bound to a local: that would copy a reference
  # return (and "_" would be a reserved name at global scope).
  _gentest_cdr_scan(returns CODEGEN_ARGS --quiet-clang CONTENT [=[
#pragma once
#include <vector>
namespace ret {
struct NonCopyable {
    NonCopyable(const NonCopyable &) = delete;
};
[[using gentest: test("noncopyable")]] NonCopyable &noncopyable();
[[using gentest: test("by_value")]] int by_value();
[[using gentest: bench("table")]] const std::vector<int> &table();
[[using gentest: bench("nothing")]] void nothing();
} // namespace ret
]=])
  _gentest_cdr_expect_success("discarded results")
  _gentest_cdr_expect_registered("discarded results"
    "static_cast<void>(::ret::noncopyable())" "static_cast<void>(::ret::by_value())"
    "\"ret/table\"" "\"ret/nothing\"")
  _gentest_cdr_expect_not_registered("discarded results" "const auto _" "auto _ =")
elseif(SCENARIO STREQUAL "additive_include_boundary")
  # The include for a declaring header is derived from the compile-context
  # source directory only on a path-component boundary.
  set(_content [=[
#pragma once
namespace inc {
[[using gentest: test("included")]] inline void included() {}
} // namespace inc
]=])
  _gentest_cdr_scan(partial_component HEADER "mysrc/cases.hpp" CONTENT "${_content}"
    CODEGEN_ARGS --quiet-clang --compile-context-id "demo_target:src/cases.hpp")
  _gentest_cdr_expect_success("partial path component")
  _gentest_cdr_expect_registered("partial path component" "\"inc/included\"" "mysrc/cases.hpp\"")
  _gentest_cdr_expect_not_registered("partial path component" "#include \"src/cases.hpp\"")

  _gentest_cdr_scan(whole_component HEADER "pkg/src/cases.hpp" CONTENT "${_content}"
    CODEGEN_ARGS --quiet-clang --compile-context-id "demo_target:src/cases.hpp")
  _gentest_cdr_expect_success("whole path component")
  _gentest_cdr_expect_registered("whole path component" "#include \"src/cases.hpp\"")
else()
  message(FATAL_ERROR "Unknown SCENARIO '${SCENARIO}'")
endif()
