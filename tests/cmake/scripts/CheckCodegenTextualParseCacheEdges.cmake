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
  list(APPEND _args "${_std}" "-I${_work_dir_norm}/early" "-I${_work_dir_norm}/late" "-I${_work_dir_norm}" ${ARGN}
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

# include_next and __has_include_next do not have a complete generic lookup
# callback. They deliberately never store a cache result, so a later run is a
# miss rather than a hit.
gentest_fixture_write_file("${_work_dir}/early/next.hpp" "#pragma once\n#include_next <next.hpp>\n")
gentest_fixture_write_file("${_work_dir}/late/next.hpp" "#pragma once\ninline constexpr int next = 1;\n")
set(_next_source "${_work_dir}/next_cases.cpp")
gentest_fixture_write_file("${_next_source}" "#include <next.hpp>\n[[using gentest: test(\"cache/next\")]] void cache_next() {}\n")
_write_compdb("${_next_source}")
_run("${_next_source}" next_cold miss)
_run("${_next_source}" next_again miss)

gentest_fixture_write_file("${_work_dir}/early/has_next.hpp" [=[
#pragma once
#if __has_include_next(<next.hpp>)
#include_next <next.hpp>
#endif
]=])
set(_has_next_source "${_work_dir}/has_next_cases.cpp")
gentest_fixture_write_file("${_has_next_source}" "#include <has_next.hpp>\n[[using gentest: test(\"cache/has_next\")]] void cache_has_next() {}\n")
_write_compdb("${_has_next_source}")
_run("${_has_next_source}" has_next_cold miss)
_run("${_has_next_source}" has_next_again miss)

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
_run("${_volatile_source}" volatile_cold miss)
_run("${_volatile_source}" volatile_again miss)

set(_overlay "${_work_dir}/empty-overlay.yaml")
gentest_fixture_write_file("${_overlay}" "{ 'version': 0, 'roots': [] }\n")
_write_compdb("${_forced_source}" "-ivfsoverlay" "${_overlay}")
_run("${_forced_source}" overlay_bypass bypass)

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
