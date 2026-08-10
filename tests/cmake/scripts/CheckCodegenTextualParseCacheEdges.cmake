if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCacheEdges.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCacheEdges.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCacheEdges.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

find_program(_clang NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_clang}" _clang_norm)

set(_work_dir "${BUILD_ROOT}/codegen_textual_parse_cache_edges")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/early" "${_work_dir}/late" "${_work_dir}/generated")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
set(_cache_dir "${_work_dir}/cache")

function(_write_compdb source)
  set(_compiler "${_clang_norm}")
  if(DEFINED _cache_test_compiler AND NOT "${_cache_test_compiler}" STREQUAL "")
    set(_compiler "${_cache_test_compiler}")
  endif()
  set(_args "${_compiler}")
  if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
    list(APPEND _args "${TARGET_ARG}")
  endif()
  gentest_normalize_std_flag_for_compiler(_std "${_compiler}" "${CODEGEN_STD}")
  list(APPEND _args "${_std}" ${_cache_test_include_prefix} "-I${_work_dir_norm}/early" "-I${_work_dir_norm}/late"
    "-I${_work_dir_norm}" ${ARGN}
    "-c" "${source}" "-o" "${_work_dir}/object.o")
  gentest_fixture_make_compdb_entry(_entry DIRECTORY "${_work_dir_norm}" FILE "${source}" ARGUMENTS ${_args})
  gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_entry}")
endfunction()

function(_run source label expected_cache)
  set(_output_dir "${_work_dir}/generated/${label}")
  file(MAKE_DIRECTORY "${_output_dir}")
  set(_timing "${_output_dir}/timing.json")
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --parse-cache-dir "${_cache_dir}"
      --timing-json "${_timing}"
      --tu-out-dir "${_output_dir}"
      --tu-header-output "${_output_dir}/cases.gentest.h"
      ${ARGN}
      --compdb "${_work_dir}"
      "${source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "cache edge '${label}' failed.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  file(READ "${_timing}" _timing_text)
  string(REGEX MATCH "\\\"cache\\\"[ \t]*:[ \t]*\\\"${expected_cache}\\\"" _match "${_timing_text}")
  if("${_match}" STREQUAL "")
    message(FATAL_ERROR "Expected cache=${expected_cache} for '${label}'.\n${_timing_text}")
  endif()
endfunction()

function(_run_uncacheable_twice source label)
  file(GLOB _entries_before "${_cache_dir}/*.json")
  list(LENGTH _entries_before _entry_count_before)
  _run("${source}" "${label}_first" bypass ${ARGN})
  _run("${source}" "${label}_second" bypass ${ARGN})
  file(GLOB _entries_after "${_cache_dir}/*.json")
  list(LENGTH _entries_after _entry_count_after)
  if(NOT _entry_count_after EQUAL _entry_count_before)
    message(FATAL_ERROR
      "Uncacheable edge '${label}' persisted a parse-cache entry (${_entry_count_before} -> ${_entry_count_after})")
  endif()
endfunction()

# Physical identity participates in a fingerprint even when the header bytes
# are unchanged: a symlink target or inode/hard-link replacement can change
# Clang FileEntry / pragma-once behavior.
set(_link_source "${_work_dir}/link_cases.cpp")
gentest_fixture_write_file("${_work_dir}/link_a.hpp" "#pragma once\ninline constexpr int linked = 1;\n")
gentest_fixture_write_file("${_work_dir}/link_b.hpp" "#pragma once\ninline constexpr int linked = 1;\n")
gentest_fixture_write_file("${_link_source}" "#include \"link.hpp\"\n[[using gentest: test(\"cache/link\")]] void cache_link() {}\n")
if(NOT WIN32)
  file(CREATE_LINK "${_work_dir}/link_a.hpp" "${_work_dir}/link.hpp" SYMBOLIC RESULT _link_error)
  if(NOT "${_link_error}" STREQUAL "0")
    message(STATUS "GENTEST_SKIP_TEST: could not create symlink: ${_link_error}")
  else()
    _write_compdb("${_link_source}")
    _run("${_link_source}" link_cold miss)
    _run("${_link_source}" link_hit hit)
    file(REMOVE "${_work_dir}/link.hpp")
    file(CREATE_LINK "${_work_dir}/link_b.hpp" "${_work_dir}/link.hpp" SYMBOLIC RESULT _retarget_error)
    if(NOT "${_retarget_error}" STREQUAL "0")
      message(FATAL_ERROR "Could not retarget parse-cache symlink fixture: ${_retarget_error}")
    endif()
    _run("${_link_source}" link_retarget miss)
  endif()

  gentest_fixture_write_file("${_work_dir}/hard_a.hpp" "#pragma once\ninline constexpr int hard = 1;\n")
  gentest_fixture_write_file("${_work_dir}/hard_b.hpp" "#pragma once\ninline constexpr int hard = 1;\n")
  gentest_fixture_write_file("${_work_dir}/hard_cases.cpp" "#include \"hard.hpp\"\n[[using gentest: test(\"cache/hard\")]] void cache_hard() {}\n")
  file(CREATE_LINK "${_work_dir}/hard_a.hpp" "${_work_dir}/hard.hpp" RESULT _hard_link_error)
  if(NOT "${_hard_link_error}" STREQUAL "0")
    message(STATUS "GENTEST_SKIP_TEST: could not create hard-link fixture: ${_hard_link_error}")
  else()
    _write_compdb("${_work_dir}/hard_cases.cpp")
    _run("${_work_dir}/hard_cases.cpp" hard_cold miss)
    _run("${_work_dir}/hard_cases.cpp" hard_hit hit)
    file(REMOVE "${_work_dir}/hard.hpp")
    file(CREATE_LINK "${_work_dir}/hard_b.hpp" "${_work_dir}/hard.hpp" RESULT _hard_replace_error)
    if(NOT "${_hard_replace_error}" STREQUAL "0")
      message(FATAL_ERROR "Could not replace hard-link parse-cache fixture: ${_hard_replace_error}")
    endif()
    _run("${_work_dir}/hard_cases.cpp" hard_replace miss)
  endif()
endif()

# A successful/failed __has_include must guard its lookup candidates. Adding a
# previously absent earlier-root probe header must not accept the old result.
set(_probe_source "${_work_dir}/probe_cases.cpp")
gentest_fixture_write_file("${_work_dir}/probe.hpp" [=[
#if __has_include(<optional_probe.hpp>)
#include <optional_probe.hpp>
#endif
]=])
gentest_fixture_write_file("${_probe_source}" "#include \"probe.hpp\"\n[[using gentest: test(\"cache/probe\")]] void cache_probe() {}\n")
_write_compdb("${_probe_source}")
_run("${_probe_source}" probe_cold miss)
_run("${_probe_source}" probe_hit hit)
gentest_fixture_write_file("${_work_dir}/early/optional_probe.hpp" "#pragma once\ninline constexpr int optional_probe = 1;\n")
_run("${_probe_source}" probe_shadow miss)

# Clang drops nonexistent include roots from HeaderSearch, so preprocessing
# callbacks cannot emit lookup guards below them. Their existence state is
# part of the cache context: creating an earlier configured root must force a
# miss before that root can shadow a later header.
set(_appearing_root "${_work_dir}/appearing")
file(TO_CMAKE_PATH "${_appearing_root}" _appearing_root_norm)
set(_root_source "${_work_dir}/root_cases.cpp")
gentest_fixture_write_file("${_work_dir}/late/root_appearance.hpp"
  "[[using gentest: test(\"cache/root_late\")]] inline void cache_root_case() {}\n")
gentest_fixture_write_file("${_root_source}" "#include <root_appearance.hpp>\n")
set(_cache_test_include_prefix "-I${_appearing_root_norm}")
_write_compdb("${_root_source}")
_run("${_root_source}" root_missing_cold miss)
_run("${_root_source}" root_missing_hit hit)
file(MAKE_DIRECTORY "${_appearing_root}")
gentest_fixture_write_file("${_appearing_root}/root_appearance.hpp"
  "[[using gentest: test(\"cache/root_early\")]] inline void cache_root_case() {}\n")
_run("${_root_source}" root_appeared miss)
file(READ "${_work_dir}/generated/root_appeared/cases.gentest.h" _root_appeared_output)
if(NOT _root_appeared_output MATCHES "cache/root_early")
  message(FATAL_ERROR "Appearing include root did not replace the later header.\n${_root_appeared_output}")
endif()
unset(_cache_test_include_prefix)

# include_next and __has_include_next do not have a complete generic lookup
# callback. They deliberately bypass and never store a cache result.
gentest_fixture_write_file("${_work_dir}/early/next.hpp" "#pragma once\n#include_next <next.hpp>\n")
gentest_fixture_write_file("${_work_dir}/late/next.hpp" "#pragma once\ninline constexpr int next = 1;\n")
set(_next_source "${_work_dir}/next_cases.cpp")
gentest_fixture_write_file("${_next_source}" "#include <next.hpp>\n[[using gentest: test(\"cache/next\")]] void cache_next() {}\n")
_write_compdb("${_next_source}")
_run_uncacheable_twice("${_next_source}" next)

gentest_fixture_write_file("${_work_dir}/early/has_next.hpp" [=[
#pragma once
#if __has_include_next(<next.hpp>)
#include_next <next.hpp>
#endif
]=])
set(_has_next_source "${_work_dir}/has_next_cases.cpp")
gentest_fixture_write_file("${_has_next_source}" "#include <has_next.hpp>\n[[using gentest: test(\"cache/has_next\")]] void cache_has_next() {}\n")
_write_compdb("${_has_next_source}")
_run_uncacheable_twice("${_has_next_source}" has_next)

# Absolute forced headers are direct fingerprints; relative forced-input and
# module-bearing command lines are intentionally marked bypass before lookup.
set(_forced_source "${_work_dir}/forced_cases.cpp")
set(_forced_header "${_work_dir}/forced.hpp")
gentest_fixture_write_file("${_forced_header}" "#pragma once\ninline constexpr int forced = 1;\n")
gentest_fixture_write_file("${_forced_source}" "[[using gentest: test(\"cache/forced\")]] void cache_forced() {}\n")
_write_compdb("${_forced_source}" "-include" "${_forced_header}")
_run("${_forced_source}" forced_cold miss)
_run("${_forced_source}" forced_hit hit)
gentest_fixture_write_file("${_forced_header}" "#pragma once\ninline constexpr int forced = 2;\n")
_run("${_forced_source}" forced_changed miss)
_write_compdb("${_forced_source}" "-include" "forced.hpp")
_run("${_forced_source}" forced_relative bypass)
_write_compdb("${_forced_source}" "-fmodules")
_run("${_forced_source}" modules_bypass bypass)
_write_compdb("${_forced_source}" "-Xclang" "-chain-include" "-Xclang" "${_forced_header}")
_run("${_forced_source}" chain_include_bypass bypass)

# Predefined time macros and VFS overlays are intentionally uncacheable. The
# former changes without an input file edit; an overlay can rewrite lookup
# without changing the command-line path spelling.
set(_volatile_source "${_work_dir}/volatile_cases.cpp")
gentest_fixture_write_file("${_volatile_source}" [=[
constexpr const char *cache_date = __DATE__;
constexpr const char *cache_timestamp = __TIMESTAMP__;
[[using gentest: test("cache/volatile")]]
void cache_volatile() {}
]=])
_write_compdb("${_volatile_source}")
_run_uncacheable_twice("${_volatile_source}" volatile)

# Clang 22 exposes dedicated embed callbacks. Supported older Clang releases
# do not, so Gentest conservatively scans entered source buffers and bypasses
# caching whenever #embed or __has_embed appears.
execute_process(COMMAND "${_clang_norm}" --version OUTPUT_VARIABLE _clang_version_text ERROR_QUIET)
string(REGEX MATCH "clang version ([0-9]+)" _clang_version_match "${_clang_version_text}")
if(NOT "${_clang_version_match}" STREQUAL "" AND CMAKE_MATCH_1 GREATER_EQUAL 20)
  set(_embed_source "${_work_dir}/embed_cases.cpp")
  set(_embed_payload "${_work_dir}/embed_payload.bin")
  gentest_fixture_write_file("${_embed_payload}" "a")
  gentest_fixture_write_file("${_embed_source}" [=[
#if __has_embed("embed_payload.bin")
inline constexpr bool cache_embed_present = true;
#else
inline constexpr bool cache_embed_present = false;
#endif
[[using gentest: test("cache/embed")]] void cache_embed() {}
]=])
  _write_compdb("${_embed_source}")
  _run_uncacheable_twice("${_embed_source}" embed)
  gentest_fixture_write_file("${_embed_payload}" "b")
  _run("${_embed_source}" embed_changed bypass)

  set(_digraph_embed_source "${_work_dir}/digraph_embed_cases.cpp")
  gentest_fixture_write_file("${_digraph_embed_source}" [=[
constexpr unsigned char cache_digraph_embed[] = {
%:embed "embed_payload.bin"
};
[[using gentest: test("cache/digraph_embed")]] void cache_digraph_embed_case() {}
]=])
  _write_compdb("${_digraph_embed_source}")
  _run_uncacheable_twice("${_digraph_embed_source}" digraph_embed)
endif()

set(_overlay "${_work_dir}/empty-overlay.yaml")
gentest_fixture_write_file("${_overlay}" "{ 'version': 0, 'roots': [] }\n")
_write_compdb("${_forced_source}" "-ivfsoverlay" "${_overlay}")
_run("${_forced_source}" overlay_bypass bypass)

# Sanitizer/coverage ignorelists are driver inputs rather than preprocessing
# dependencies. Replacing one must be observed by a cold parse.
set(_sanitize_ignorelist "${_work_dir}/sanitize-ignorelist.txt")
gentest_fixture_write_file("${_sanitize_ignorelist}" "fun:ignored_function\n")
_write_compdb("${_forced_source}" "-fsanitize=undefined" "-fsanitize-ignorelist=${_sanitize_ignorelist}")
_run_uncacheable_twice("${_forced_source}" sanitizer_ignorelist)
gentest_fixture_write_file("${_sanitize_ignorelist}" "not a valid special-case-list entry\n")
execute_process(
  COMMAND "${PROG}"
    --jobs=1
    --check
    --parse-cache-dir "${_cache_dir}"
    --compdb "${_work_dir}"
    "${_forced_source}"
  RESULT_VARIABLE _invalid_ignorelist_rc
  OUTPUT_VARIABLE _invalid_ignorelist_out
  ERROR_VARIABLE _invalid_ignorelist_err)
if(_invalid_ignorelist_rc EQUAL 0)
  message(FATAL_ERROR
    "Replacing a sanitizer ignorelist was hidden by a parse-cache hit.\n"
    "${_invalid_ignorelist_out}\n${_invalid_ignorelist_err}")
endif()

# Serialized AST merge inputs are outside preprocessing dependency callbacks.
# Even a valid AST must therefore bypass rather than become a stale cache hit
# when the serialized input is replaced in place.
set(_ast_input_source "${_work_dir}/ast_input.cpp")
set(_ast_input "${_work_dir}/ast_input.ast")
set(_ast_merge_source "${_work_dir}/ast_merge_cases.cpp")
gentest_fixture_write_file("${_ast_input_source}" "struct AstMergeInput { int value; };\n")
gentest_fixture_write_file("${_ast_merge_source}"
  "[[using gentest: test(\"cache/ast_merge\")]] void cache_ast_merge() {}\n")
gentest_normalize_std_flag_for_compiler(_ast_std "${_clang_norm}" "${CODEGEN_STD}")
execute_process(
  COMMAND "${_clang_norm}" "${_ast_std}" -emit-ast "${_ast_input_source}" -o "${_ast_input}"
  RESULT_VARIABLE _ast_rc
  OUTPUT_VARIABLE _ast_out
  ERROR_VARIABLE _ast_err)
if(NOT _ast_rc EQUAL 0)
  message(FATAL_ERROR "Could not create serialized AST cache fixture.\n${_ast_out}\n${_ast_err}")
endif()
_write_compdb("${_ast_merge_source}" "-Xclang" "-ast-merge" "-Xclang" "${_ast_input}")
_run_uncacheable_twice("${_ast_merge_source}" ast_merge)
gentest_fixture_write_file("${_ast_input_source}" "struct AstMergeInput { long value; };\n")
execute_process(
  COMMAND "${_clang_norm}" "${_ast_std}" -emit-ast "${_ast_input_source}" -o "${_ast_input}"
  RESULT_VARIABLE _ast_replace_rc
  OUTPUT_VARIABLE _ast_replace_out
  ERROR_VARIABLE _ast_replace_err)
if(NOT _ast_replace_rc EQUAL 0)
  message(FATAL_ERROR "Could not replace serialized AST cache fixture.\n${_ast_replace_out}\n${_ast_replace_err}")
endif()
_run("${_ast_merge_source}" ast_merge_replaced bypass)

# cc1 file remapping consumes the replacement as a semantic input without a
# preprocessing dependency callback, so it must never authorize a cache hit.
set(_remap_source "${_work_dir}/remap_cases.cpp")
set(_remap_replacement "${_work_dir}/remap_replacement.cpp")
gentest_fixture_write_file("${_remap_source}"
  "[[using gentest: test(\"cache/remap\")]] void cache_remap() {}\n")
gentest_fixture_write_file("${_remap_replacement}"
  "[[using gentest: test(\"cache/remap\")]] void cache_remap() {}\n")
_write_compdb("${_remap_source}" "-Xclang" "-remap-file" "-Xclang" "__GENTEST_REMAP_PAIR__")
file(READ "${_work_dir}/compile_commands.json" _remap_compdb)
string(REPLACE "__GENTEST_REMAP_PAIR__" "${_remap_source};${_remap_replacement}" _remap_compdb "${_remap_compdb}")
file(WRITE "${_work_dir}/compile_commands.json" "${_remap_compdb}")
_run_uncacheable_twice("${_remap_source}" remap_file)
gentest_fixture_write_file("${_remap_replacement}"
  "[[using gentest: test(\"cache/remap_changed\")]] void cache_remap_changed() {}\n")
_run("${_remap_source}" remap_file_replaced bypass)

# OpenMP device compilation may consume a host IR file directly in cc1. That
# file is not a preprocessing dependency, so this command shape must bypass
# even while the IR is valid and the real parse succeeds.
set(_openmp_host_source "${_work_dir}/openmp_host.cpp")
set(_openmp_host_ir "${_work_dir}/openmp_host.bc")
gentest_fixture_write_file("${_openmp_host_source}" "int gentest_openmp_host_value() { return 1; }\n")
execute_process(
  COMMAND "${_clang_norm}" "${_ast_std}" -fopenmp -emit-llvm -c "${_openmp_host_source}" -o "${_openmp_host_ir}"
  RESULT_VARIABLE _openmp_host_rc
  OUTPUT_VARIABLE _openmp_host_out
  ERROR_VARIABLE _openmp_host_err)
if(_openmp_host_rc EQUAL 0)
  _write_compdb("${_forced_source}"
    -fopenmp -Xclang -fopenmp-host-ir-file-path -Xclang "${_openmp_host_ir}")
  _run_uncacheable_twice("${_forced_source}" openmp_host_ir)
else()
  message(STATUS
    "Skipping OpenMP host-IR parse-cache probe because the compiler could not create host IR: ${_openmp_host_err}")
endif()

# Sysroot-derived include roots are resolved by Clang, not reconstructed by a
# parallel scanner. Until preprocessing callbacks run, these command shapes
# therefore bypass textual caching conservatively.
_write_compdb("${_forced_source}" "--sysroot=/")
_run_uncacheable_twice("${_forced_source}" sysroot_joined)
_write_compdb("${_forced_source}" "-isysroot" "/")
_run_uncacheable_twice("${_forced_source}" sysroot_split)
_write_compdb("${_forced_source}" "-iwithsysroot" "/usr/include")
_run_uncacheable_twice("${_forced_source}" include_with_sysroot)

# Randomized-record-layout seed files are semantic cc1 inputs but are not
# preprocessing dependencies. A syntactically valid seed must never authorize
# a parse-cache hit after the file changes in place.
set(_layout_seed "${_work_dir}/layout.seed")
gentest_fixture_write_file("${_layout_seed}" "gentest-layout-seed\n")
_write_compdb("${_forced_source}" "-frandomize-layout-seed-file=${_layout_seed}")
_run_uncacheable_twice("${_forced_source}" randomize_layout_seed)
gentest_fixture_write_file("${_layout_seed}" "replacement-layout-seed\n")
_run("${_forced_source}" randomize_layout_seed_replaced bypass)

# Profile-guided commands can consume data files that Clang does not report
# through preprocessing callbacks. Keep every profile mode conservative, and
# prove that replacing a previously valid indexed profile is observed by a
# real parse rather than hidden behind a cache hit.
_write_compdb("${_forced_source}" "-fprofile-instr-generate")
_run("${_forced_source}" profile_generate_bypass bypass)

get_filename_component(_clang_bin_dir "${_clang_norm}" DIRECTORY)
find_program(
  _llvm_profdata
  NAMES llvm-profdata llvm-profdata-23 llvm-profdata-22 llvm-profdata-21 llvm-profdata-20 llvm-profdata-19
  HINTS "${_clang_bin_dir}")
if(_llvm_profdata)
  set(_profile_program_source "${_work_dir}/profile_program.cpp")
  set(_profile_program "${_work_dir}/profile_program${CMAKE_EXECUTABLE_SUFFIX}")
  if(CMAKE_HOST_WIN32)
    set(_profile_program "${_work_dir}/profile_program.exe")
  endif()
  set(_profile_raw "${_work_dir}/profile.profraw")
  set(_profile_data "${_work_dir}/profile.profdata")
  gentest_fixture_write_file("${_profile_program_source}" "int main() { return 0; }\n")
  execute_process(
    COMMAND "${_clang_norm}" "${_ast_std}" "-fprofile-instr-generate=${_profile_raw}" "${_profile_program_source}" -o "${_profile_program}"
    RESULT_VARIABLE _profile_compile_rc
    OUTPUT_VARIABLE _profile_compile_out
    ERROR_VARIABLE _profile_compile_err)
  if(NOT _profile_compile_rc EQUAL 0)
    message(FATAL_ERROR "Could not compile profile fixture.\n${_profile_compile_out}\n${_profile_compile_err}")
  endif()
  execute_process(
    COMMAND "${_profile_program}"
    RESULT_VARIABLE _profile_run_rc
    OUTPUT_VARIABLE _profile_run_out
    ERROR_VARIABLE _profile_run_err)
  if(NOT _profile_run_rc EQUAL 0 OR NOT EXISTS "${_profile_raw}")
    message(FATAL_ERROR "Could not record profile fixture.\n${_profile_run_out}\n${_profile_run_err}")
  endif()
  execute_process(
    COMMAND "${_llvm_profdata}" merge -o "${_profile_data}" "${_profile_raw}"
    RESULT_VARIABLE _profile_merge_rc
    OUTPUT_VARIABLE _profile_merge_out
    ERROR_VARIABLE _profile_merge_err)
  if(NOT _profile_merge_rc EQUAL 0)
    message(FATAL_ERROR "Could not index profile fixture.\n${_profile_merge_out}\n${_profile_merge_err}")
  endif()

  _write_compdb("${_forced_source}" "-fprofile-instr-use=${_profile_data}")
  _run_uncacheable_twice("${_forced_source}" profile_input)
  gentest_fixture_write_file("${_profile_data}" "not an indexed profile\n")
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --check
      --parse-cache-dir "${_cache_dir}"
      --compdb "${_work_dir}"
      "${_forced_source}"
    RESULT_VARIABLE _invalid_profile_rc
    OUTPUT_VARIABLE _invalid_profile_out
    ERROR_VARIABLE _invalid_profile_err)
  if(_invalid_profile_rc EQUAL 0)
    message(FATAL_ERROR
      "Replacing a profile input was hidden by a parse-cache hit.\n"
      "${_invalid_profile_out}\n${_invalid_profile_err}")
  endif()
endif()

# quiet-clang affects captured diagnostics and is part of the parse policy.
# A normal warning cache must neither be accepted nor replayed in quiet mode.
set(_warning_source "${_work_dir}/warning_cases.cpp")
gentest_fixture_write_file("${_warning_source}" "#warning cache-warning\n[[using gentest: test(\"cache/warning\")]] void cache_warning() {}\n")
_write_compdb("${_warning_source}")
_run("${_warning_source}" warning_normal_cold miss)
_run("${_warning_source}" warning_normal_hit hit)
function(_run_quiet_warning expected_cache)
  set(_output_dir "${_work_dir}/generated/warning_quiet_${expected_cache}")
  file(MAKE_DIRECTORY "${_output_dir}")
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --quiet-clang
      --parse-cache-dir "${_cache_dir}"
      --timing-json "${_output_dir}/timing.json"
      --tu-out-dir "${_output_dir}"
      --tu-header-output "${_output_dir}/cases.gentest.h"
      --compdb "${_work_dir}"
      "${_warning_source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "quiet cache policy invocation failed.\n${_out}\n${_err}")
  endif()
  file(READ "${_output_dir}/timing.json" _timing_text)
  string(REGEX MATCH "\\\"cache\\\"[ \t]*:[ \t]*\\\"${expected_cache}\\\"" _cache_match "${_timing_text}")
  string(FIND "${_err}" "cache-warning" _warning_pos)
  if("${_cache_match}" STREQUAL "" OR NOT _warning_pos EQUAL -1)
    message(FATAL_ERROR "quiet-clang cache policy crossed diagnostics or missed its cache state.\n${_timing_text}\n${_err}")
  endif()
endfunction()
_run_quiet_warning(miss)
_run_quiet_warning(hit)

# ClangTool runs every compilation-database command registered for a source.
# Cache entries are deliberately bypassed for that uncommon shape until every
# command participates in the key and can be replayed independently.
set(_multi_command_source "${_work_dir}/multi_command_cases.cpp")
gentest_fixture_write_file("${_multi_command_source}" [=[
#if MULTI_COMMAND_VALUE == 1
[[using gentest: test("cache/multi_one")]] void cache_multi_one() {}
#elif MULTI_COMMAND_VALUE == 2
[[using gentest: test("cache/multi_two")]] void cache_multi_two() {}
#endif
]=])
set(_multi_command_common "${_clang_norm}")
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _multi_command_common "${TARGET_ARG}")
endif()
gentest_normalize_std_flag_for_compiler(_multi_command_std "${_clang_norm}" "${CODEGEN_STD}")
list(APPEND _multi_command_common "${_multi_command_std}" "-I${_work_dir_norm}" "-c" "${_multi_command_source}")
set(_multi_command_zero ${_multi_command_common})
list(INSERT _multi_command_zero 2 "-DMULTI_COMMAND_VALUE=0")
set(_multi_command_one ${_multi_command_common})
list(INSERT _multi_command_one 2 "-DMULTI_COMMAND_VALUE=1")
gentest_fixture_make_compdb_entry(_multi_command_zero_entry DIRECTORY "${_work_dir_norm}" FILE "${_multi_command_source}"
  ARGUMENTS ${_multi_command_zero})
gentest_fixture_make_compdb_entry(_multi_command_one_entry DIRECTORY "${_work_dir_norm}" FILE "${_multi_command_source}"
  ARGUMENTS ${_multi_command_one})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_multi_command_zero_entry}" "${_multi_command_one_entry}")
_run("${_multi_command_source}" multi_command_one bypass)

set(_multi_command_two ${_multi_command_common})
list(INSERT _multi_command_two 2 "-DMULTI_COMMAND_VALUE=2")
gentest_fixture_make_compdb_entry(_multi_command_two_entry DIRECTORY "${_work_dir_norm}" FILE "${_multi_command_source}"
  ARGUMENTS ${_multi_command_two})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_multi_command_zero_entry}" "${_multi_command_two_entry}")
_run("${_multi_command_source}" multi_command_two bypass)
file(READ "${_work_dir}/generated/multi_command_two/cases.gentest.h" _multi_command_header)
string(FIND "${_multi_command_header}" "cache/multi_two" _multi_two_pos)
string(FIND "${_multi_command_header}" "cache/multi_one" _multi_one_pos)
if(_multi_two_pos EQUAL -1 OR NOT _multi_one_pos EQUAL -1)
  message(FATAL_ERROR "A later compilation-database command change did not reach a fresh parse.\n${_multi_command_header}")
endif()

# Gentest's own nonfatal diagnostics are captured with Clang diagnostics so a
# cache hit reports exactly the warning emitted by a cold parse.
set(_gentest_diag_source "${_work_dir}/gentest_diag_cases.cpp")
gentest_fixture_write_file("${_gentest_diag_source}" [=[
[[using foreign: test("ignored")]] void cache_foreign_attribute() {}
[[using gentest: test("cache/gentest_diagnostic")]] void cache_gentest_diagnostic() {}
]=])
_write_compdb("${_gentest_diag_source}")
function(_run_gentest_diagnostic label expected_cache)
  set(_output_dir "${_work_dir}/generated/${label}")
  file(MAKE_DIRECTORY "${_output_dir}")
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --parse-cache-dir "${_cache_dir}"
      --timing-json "${_output_dir}/timing.json"
      --tu-out-dir "${_output_dir}"
      --tu-header-output "${_output_dir}/cases.gentest.h"
      --compdb "${_work_dir}"
      "${_gentest_diag_source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Gentest diagnostic cache invocation failed.\n${_out}\n${_err}")
  endif()
  file(READ "${_output_dir}/timing.json" _timing_text)
  string(REGEX MATCH "\"cache\"[ \t]*:[ \t]*\"${expected_cache}\"" _cache_match "${_timing_text}")
  string(REGEX MATCHALL "unsupported attribute namespace" _diagnostic_matches "${_err}")
  list(LENGTH _diagnostic_matches _diagnostic_count)
  if("${_cache_match}" STREQUAL "" OR NOT _diagnostic_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one Gentest diagnostic with cache=${expected_cache}; got ${_diagnostic_count}.\n${_timing_text}\n${_err}")
  endif()
endfunction()
_run_gentest_diagnostic(gentest_diag_cold miss)
_run_gentest_diagnostic(gentest_diag_hit hit)

# Default driver config files are expanded before the cc1 command is keyed.
# Point the driver's default config search at a temporary system directory;
# this avoids depending on the host's installed configuration path.
if(NOT WIN32)
  execute_process(COMMAND "${_clang_norm}" -dumpmachine RESULT_VARIABLE _triple_rc OUTPUT_VARIABLE _target_triple
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT _triple_rc EQUAL 0 OR "${_target_triple}" STREQUAL "")
    message(STATUS "GENTEST_SKIP_TEST: could not determine clang target triple for default-config fixture")
  else()
    set(_config_source "${_work_dir}/config_cases.cpp")
    gentest_fixture_write_file("${_config_source}" [=[
#if CACHE_CONFIG_VALUE
[[using gentest: test("cache/config_one")]]
void cache_config_one() {}
#else
[[using gentest: test("cache/config_zero")]]
void cache_config_zero() {}
#endif
]=])
    set(_config_system_dir "${_work_dir}/config-system")
    file(MAKE_DIRECTORY "${_config_system_dir}")
    set(_config_file "${_config_system_dir}/${_target_triple}-clang++.cfg")
    set(_config_forced_header "${_work_dir}/config-forced.hpp")
    gentest_fixture_write_file("${_config_forced_header}" "#pragma once\ninline constexpr int config_forced = 1;\n")
    gentest_fixture_write_file("${_config_file}" "-DCACHE_CONFIG_VALUE=0\n-include\n${_config_forced_header}\n")
    _write_compdb("${_config_source}" "--config-system-dir=${_config_system_dir}")
    _run("${_config_source}" config_cold miss)
    _run("${_config_source}" config_hit hit)
    gentest_fixture_write_file("${_config_forced_header}" "#pragma once\ninline constexpr int config_forced = 2;\n")
    _run("${_config_source}" config_forced_changed miss)
    gentest_fixture_write_file("${_config_file}" "-DCACHE_CONFIG_VALUE=1\n-include\n${_config_forced_header}\n")
    _run("${_config_source}" config_changed miss)
    file(READ "${_work_dir}/generated/config_changed/cases.gentest.h" _config_header)
    string(FIND "${_config_header}" "cache/config_one" _config_one_pos)
    if(_config_one_pos EQUAL -1)
      message(FATAL_ERROR "Default config change did not reach the effective parse command.\n${_config_header}")
    endif()
  endif()
endif()

# Relocating an otherwise identical build tree changes the physical source
# context. It must safely parse again rather than accidentally consuming the
# old tree's entry.
file(MAKE_DIRECTORY "${_work_dir}/relocated-a" "${_work_dir}/relocated-b")
set(_relocated_a "${_work_dir}/relocated-a/cases.cpp")
set(_relocated_b "${_work_dir}/relocated-b/cases.cpp")
gentest_fixture_write_file("${_relocated_a}" "[[using gentest: test(\"cache/relocated\")]] void cache_relocated() {}\n")
file(COPY "${_relocated_a}" DESTINATION "${_work_dir}/relocated-b")
_write_compdb("${_relocated_a}")
_run("${_relocated_a}" relocated_a_cold miss)
_run("${_relocated_a}" relocated_a_hit hit)
_write_compdb("${_relocated_b}")
_run("${_relocated_b}" relocated_b_miss miss)

# Two independent generator processes may populate the same slot. Both writes
# are best effort; after the race a normal run must still be a correct hit.
if(NOT WIN32)
  find_program(_sh NAMES sh)
  if(_sh)
    set(_race_source "${_work_dir}/race_cases.cpp")
    gentest_fixture_write_file("${_race_source}" "[[using gentest: test(\"cache/race\")]] void cache_race() {}\n")
    _write_compdb("${_race_source}")
    file(REMOVE_RECURSE "${_cache_dir}")
    gentest_fixture_join_posix_shell_command(_race_a "${PROG}" --jobs=1 --parse-cache-dir "${_cache_dir}" --tu-out-dir
      "${_work_dir}/generated/race_a" --tu-header-output "${_work_dir}/generated/race_a/cases.gentest.h" --compdb "${_work_dir}" "${_race_source}")
    gentest_fixture_join_posix_shell_command(_race_b "${PROG}" --jobs=1 --parse-cache-dir "${_cache_dir}" --tu-out-dir
      "${_work_dir}/generated/race_b" --tu-header-output "${_work_dir}/generated/race_b/cases.gentest.h" --compdb "${_work_dir}" "${_race_source}")
    execute_process(
      COMMAND "${_sh}" -c "${_race_a} & p1=$!; ${_race_b} & p2=$!; wait \"$p1\"; a=$?; wait \"$p2\"; b=$?; test \"$a\" -eq 0 && test \"$b\" -eq 0"
      RESULT_VARIABLE _race_rc
      OUTPUT_VARIABLE _race_out
      ERROR_VARIABLE _race_err)
    if(NOT _race_rc EQUAL 0)
      message(FATAL_ERROR "Concurrent parse-cache writers failed.\n--- stdout ---\n${_race_out}\n--- stderr ---\n${_race_err}")
    endif()
    foreach(_race_header IN ITEMS "${_work_dir}/generated/race_a/cases.gentest.h" "${_work_dir}/generated/race_b/cases.gentest.h")
      if(NOT EXISTS "${_race_header}")
        message(FATAL_ERROR "Concurrent parse-cache writer did not produce '${_race_header}'")
      endif()
    endforeach()
    _run("${_race_source}" race_hit hit)
  endif()
endif()
