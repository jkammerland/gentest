if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCache.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCache.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTextualParseCache.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

find_program(_clang NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_clang}" _clang_norm)

set(_work_dir "${BUILD_ROOT}/codegen_textual_parse_cache")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/early" "${_work_dir}/late" "${_work_dir}/generated")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_source "${_work_dir}/cases.cpp")
set(_private_header "${_work_dir}/private.hpp")
set(_shared_header "${_work_dir}/shared.hpp")
set(_late_shadow "${_work_dir}/late/shadow.hpp")
set(_early_shadow "${_work_dir}/early/shadow.hpp")
set(_cache_dir "${_work_dir}/cache")
set(_out_dir "${_work_dir}/generated")
set(_header "${_out_dir}/cases.gentest.h")
set(_manifest "${_out_dir}/cases.artifact_manifest.json")
set(_depfile "${_out_dir}/cases.d")
set(_timing "${_out_dir}/timing.json")

gentest_fixture_write_file("${_private_header}" "#pragma once\ninline constexpr int private_value = 1;\n")
gentest_fixture_write_file("${_shared_header}" "#pragma once\ninline constexpr int shared_value = 1;\n")
gentest_fixture_write_file("${_late_shadow}" "#pragma once\ninline constexpr int shadow_value = 1;\n")
gentest_fixture_write_file("${_source}" [=[
#include "private.hpp"
#include "shared.hpp"
#include <shadow.hpp>

#if CACHE_FLAG
[[using gentest: test("cache/flag_one")]]
void cache_flag_one() {}
#else
[[using gentest: test("cache/flag_zero")]]
void cache_flag_zero() {}
#endif
]=])

function(_write_compdb cache_flag)
  set(_args "${_clang_norm}")
  if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
    list(APPEND _args "${TARGET_ARG}")
  endif()
  gentest_normalize_std_flag_for_compiler(_std "${_clang_norm}" "${CODEGEN_STD}")
  list(APPEND _args "${_std}" "-I${_work_dir_norm}/early" "-I${_work_dir_norm}/late" "-I${_work_dir_norm}"
    "-DCACHE_FLAG=${cache_flag}" "-c" "${_source}" "-o" "${_work_dir}/cases.o")
  gentest_fixture_make_compdb_entry(_entry DIRECTORY "${_work_dir_norm}" FILE "${_source}" ARGUMENTS ${_args})
  gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_entry}")
endfunction()

function(_run_codegen expected_cache)
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --parse-cache-dir "${_cache_dir}"
      --timing-json "${_timing}"
      --tu-out-dir "${_out_dir}"
      --tu-header-output "${_header}"
      --artifact-owner-source "${_source}"
      --artifact-manifest "${_manifest}"
      --depfile "${_depfile}"
      --compdb "${_work_dir}"
      "${_source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "textual parse-cache codegen failed.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  if(NOT EXISTS "${_timing}")
    message(FATAL_ERROR "Expected timing output '${_timing}'")
  endif()
  file(READ "${_timing}" _timing_text)
  string(REGEX MATCH "\\\"cache\\\"[ \t]*:[ \t]*\\\"${expected_cache}\\\"" _cache_match "${_timing_text}")
  if("${_cache_match}" STREQUAL "")
    message(FATAL_ERROR "Expected cache=${expected_cache}. Timing JSON:\n${_timing_text}")
  endif()
endfunction()

function(_assert_output_stable previous_header previous_manifest previous_depfile)
  file(READ "${_header}" _header_text)
  file(READ "${_manifest}" _manifest_text)
  file(READ "${_depfile}" _depfile_text)
  string(SHA256 _header_hash "${_header_text}")
  string(SHA256 _manifest_hash "${_manifest_text}")
  string(SHA256 _depfile_hash "${_depfile_text}")
  if(NOT "${_header_hash}" STREQUAL "${previous_header}" OR NOT "${_manifest_hash}" STREQUAL "${previous_manifest}" OR
     NOT "${_depfile_hash}" STREQUAL "${previous_depfile}")
    message(FATAL_ERROR "A cache hit changed generated header, artifact manifest, or depfile bytes")
  endif()
endfunction()

_write_compdb(0)
_run_codegen(miss)
file(READ "${_header}" _cold_header)
file(READ "${_manifest}" _cold_manifest)
file(READ "${_depfile}" _cold_depfile)
string(SHA256 _cold_header_hash "${_cold_header}")
string(SHA256 _cold_manifest_hash "${_cold_manifest}")
string(SHA256 _cold_depfile_hash "${_cold_depfile}")
_run_codegen(hit)
_assert_output_stable("${_cold_header_hash}" "${_cold_manifest_hash}" "${_cold_depfile_hash}")

# A cache entry must faithfully round-trip the nonempty fixture and mock
# vectors as well. This uses the same one-TU textual path and verifies every
# registration/mock artifact after the second (hit) invocation.
set(_fixture_mock_header "${_work_dir}/fixture_mock_service.hpp")
set(_fixture_mock_source "${_work_dir}/fixture_mock_cases.cpp")
set(_fixture_mock_out_dir "${_work_dir}/fixture-mock-generated")
set(_fixture_mock_timing "${_fixture_mock_out_dir}/timing.json")
set(_fixture_mock_tu_header "${_fixture_mock_out_dir}/fixture_mock_cases.gentest.h")
set(_fixture_mock_manifest "${_fixture_mock_out_dir}/fixture_mock_cases.artifact_manifest.json")
set(_fixture_mock_mock_manifest "${_fixture_mock_out_dir}/fixture_mock.mock_manifest.json")
set(_fixture_mock_depfile "${_fixture_mock_out_dir}/fixture_mock_cases.d")
set(_fixture_mock_registry "${_fixture_mock_out_dir}/fixture_mock_registry.hpp")
set(_fixture_mock_impl "${_fixture_mock_out_dir}/fixture_mock_impl.hpp")
set(_fixture_mock_registry_domain "${_fixture_mock_out_dir}/fixture_mock_registry__domain_0000_header.hpp")
set(_fixture_mock_impl_domain "${_fixture_mock_out_dir}/fixture_mock_impl__domain_0000_header.hpp")
file(MAKE_DIRECTORY "${_fixture_mock_out_dir}")
gentest_fixture_write_file("${_fixture_mock_header}" [=[
#pragma once
namespace cache_fixture_mock {
struct Service {
  virtual ~Service() = default;
  virtual int ping(int value) = 0;
};
} // namespace cache_fixture_mock
]=])
gentest_fixture_write_file("${_fixture_mock_source}" [=[
#include "fixture_mock_service.hpp"
#include "gentest/mock_fwd.h"

namespace cache_fixture_mock {
struct [[using gentest: fixture(suite)]] SharedFixture {
  int value = 1;
};

using ServiceMock = gentest::mock<Service>;

[[using gentest: test("cache/fixture_mock")]]
void cache_fixture_mock(SharedFixture &fixture) {
  ServiceMock mock_service;
  (void)fixture;
  (void)mock_service;
}
} // namespace cache_fixture_mock
]=])
_write_compdb(0)
# _write_compdb() is intentionally small for the basic source. Replace its
# one entry with equivalent arguments for the fixture/mock source.
set(_fixture_mock_args "${_clang_norm}")
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _fixture_mock_args "${TARGET_ARG}")
endif()
gentest_normalize_std_flag_for_compiler(_fixture_mock_std "${_clang_norm}" "${CODEGEN_STD}")
list(APPEND _fixture_mock_args "${_fixture_mock_std}" "-I${_work_dir_norm}" "-I${SOURCE_DIR}/include" "-DGENTEST_CODEGEN=1"
  "-DCACHE_FLAG=0" "-c" "${_fixture_mock_source}" "-o" "${_work_dir}/fixture_mock_cases.o")
gentest_fixture_make_compdb_entry(_fixture_mock_entry DIRECTORY "${_work_dir_norm}" FILE "${_fixture_mock_source}"
  ARGUMENTS ${_fixture_mock_args})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_fixture_mock_entry}")

function(_run_fixture_mock expected_cache)
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --discover-mocks
      --parse-cache-dir "${_cache_dir}"
      --timing-json "${_fixture_mock_timing}"
      --tu-out-dir "${_fixture_mock_out_dir}"
      --tu-header-output "${_fixture_mock_tu_header}"
      --artifact-owner-source "${_fixture_mock_source}"
      --artifact-manifest "${_fixture_mock_manifest}"
      --mock-manifest-output "${_fixture_mock_mock_manifest}"
      --mock-registry "${_fixture_mock_registry}"
      --mock-impl "${_fixture_mock_impl}"
      --mock-domain-registry-output "${_fixture_mock_registry_domain}"
      --mock-domain-impl-output "${_fixture_mock_impl_domain}"
      --depfile "${_fixture_mock_depfile}"
      --compdb "${_work_dir}"
      "${_fixture_mock_source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "fixture/mock parse-cache codegen failed.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  file(READ "${_fixture_mock_timing}" _timing_text)
  string(REGEX MATCH "\\\"cache\\\"[ \t]*:[ \t]*\\\"${expected_cache}\\\"" _cache_match "${_timing_text}")
  if("${_cache_match}" STREQUAL "")
    message(FATAL_ERROR "Expected fixture/mock cache=${expected_cache}. Timing JSON:\n${_timing_text}")
  endif()
endfunction()

_run_fixture_mock(miss)
set(_fixture_mock_artifacts
  "${_fixture_mock_tu_header}"
  "${_fixture_mock_manifest}"
  "${_fixture_mock_mock_manifest}"
  "${_fixture_mock_depfile}"
  "${_fixture_mock_registry}"
  "${_fixture_mock_impl}"
  "${_fixture_mock_registry_domain}"
  "${_fixture_mock_impl_domain}")
foreach(_artifact IN LISTS _fixture_mock_artifacts)
  if(NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "Expected fixture/mock artifact '${_artifact}'")
  endif()
  file(SHA256 "${_artifact}" _fixture_mock_hash)
  list(APPEND _fixture_mock_hashes "${_fixture_mock_hash}")
endforeach()
file(READ "${_fixture_mock_tu_header}" _fixture_mock_tu_text)
file(READ "${_fixture_mock_mock_manifest}" _fixture_mock_manifest_text)
string(FIND "${_fixture_mock_tu_text}" "SharedFixture" _fixture_pos)
string(FIND "${_fixture_mock_manifest_text}" "cache_fixture_mock::Service" _mock_pos)
if(_fixture_pos EQUAL -1 OR _mock_pos EQUAL -1)
  message(FATAL_ERROR "Fixture/mock fixture did not produce nonempty fixture and mock manifests")
endif()
_run_fixture_mock(hit)
foreach(_artifact _fixture_mock_expected_hash IN ZIP_LISTS _fixture_mock_artifacts _fixture_mock_hashes)
  file(SHA256 "${_artifact}" _fixture_mock_hit_hash)
  if(NOT _fixture_mock_hit_hash STREQUAL "${_fixture_mock_expected_hash}")
    message(FATAL_ERROR "Fixture/mock cache hit changed '${_artifact}'")
  endif()
endforeach()

# The cache-hit mock manifest can drive a standalone mock emission too. This
# makes the nonempty MockClassInfo round trip observable in generated mock
# implementation output as well as in the persisted manifest.
set(_fixture_mock_emitted_registry "${_fixture_mock_out_dir}/emitted_mock_registry.hpp")
set(_fixture_mock_emitted_impl "${_fixture_mock_out_dir}/emitted_mock_impl.hpp")
set(_fixture_mock_emitted_registry_domain "${_fixture_mock_out_dir}/emitted_mock_registry__domain_0000_header.hpp")
set(_fixture_mock_emitted_impl_domain "${_fixture_mock_out_dir}/emitted_mock_impl__domain_0000_header.hpp")
execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_fixture_mock_mock_manifest}"
    --mock-registry "${_fixture_mock_emitted_registry}"
    --mock-impl "${_fixture_mock_emitted_impl}"
    --mock-domain-registry-output "${_fixture_mock_emitted_registry_domain}"
    --mock-domain-impl-output "${_fixture_mock_emitted_impl_domain}"
  RESULT_VARIABLE _fixture_mock_emit_rc
  OUTPUT_VARIABLE _fixture_mock_emit_out
  ERROR_VARIABLE _fixture_mock_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _fixture_mock_emit_rc EQUAL 0)
  message(FATAL_ERROR "Cache-hit mock manifest could not emit mock outputs.\n${_fixture_mock_emit_out}\n${_fixture_mock_emit_err}")
endif()
file(READ "${_fixture_mock_emitted_registry_domain}" _fixture_mock_emitted_registry_text)
string(FIND "${_fixture_mock_emitted_registry_text}" "cache_fixture_mock::Service" _emitted_mock_pos)
if(_emitted_mock_pos EQUAL -1)
  message(FATAL_ERROR "Cache-hit mock manifest emitted no Service mock declaration")
endif()

# Restore the basic source database for the remaining invalidation scenarios.
_write_compdb(0)

# Exact-content dependency invalidation: private and shared headers must both
# turn a previous entry into a miss while preserving correct output emission.
gentest_fixture_write_file("${_private_header}" "#pragma once\ninline constexpr int private_value = 2;\n")
_run_codegen(miss)
gentest_fixture_write_file("${_shared_header}" "#pragma once\ninline constexpr int shared_value = 2;\n")
_run_codegen(miss)

# The ordered search roots are unchanged. A file which appears in the earlier
# root must invalidate the negative lookup guard before the late file is used.
gentest_fixture_write_file("${_early_shadow}" "#pragma once\ninline constexpr int shadow_value = 2;\n")
_run_codegen(miss)

# Effective driver arguments, policy flags, and an explicit salt are separate
# contexts. A cache entry made under one must not cross into another.
_write_compdb(1)
_run_codegen(miss)
file(READ "${_header}" _flag_header)
string(FIND "${_flag_header}" "cache/flag_one" _flag_one_pos)
if(_flag_one_pos EQUAL -1)
  message(FATAL_ERROR "Expected changed compile flag to select cache/flag_one.\n${_flag_header}")
endif()
set(ENV{GENTEST_CODEGEN_PARSE_CACHE_SALT} "cache-test-salt")
_run_codegen(miss)
unset(ENV{GENTEST_CODEGEN_PARSE_CACHE_SALT})

# A syntactically valid entry whose serialized result is changed must also be
# a safe miss. cache_key covers lookup inputs, while result_checksum covers
# parsed data which drives emission. Seed only the current unsalted context so
# the opaque glob cannot accidentally tamper a different, still-valid slot.
file(REMOVE_RECURSE "${_cache_dir}")
_run_codegen(miss)
_run_codegen(hit)
file(GLOB _entries "${_cache_dir}/*.json")
list(LENGTH _entries _entry_count)
if(_entry_count LESS 1)
  message(FATAL_ERROR "Expected at least one parse-cache entry")
endif()
set(_tampered_result FALSE)
foreach(_entry IN LISTS _entries)
  file(READ "${_entry}" _entry_text)
  string(FIND "${_entry_text}" "cache/flag_one" _flag_name_pos)
  if(NOT _flag_name_pos EQUAL -1)
    string(REPLACE "cache/flag_one" "cache/flag_two" _tampered_entry "${_entry_text}")
    gentest_fixture_write_file("${_entry}" "${_tampered_entry}")
    set(_tampered_result TRUE)
    break()
  endif()
endforeach()
if(NOT _tampered_result)
  message(FATAL_ERROR "Could not locate a cache result to tamper")
endif()
_run_codegen(miss)

# A malformed entry is likewise a safe miss. The slot name is opaque, so
# corrupt every accumulated slot after restoring the current context to a
# known cache hit.
_run_codegen(hit)
file(GLOB _entries "${_cache_dir}/*.json")
foreach(_corrupt_entry IN LISTS _entries)
  gentest_fixture_write_file("${_corrupt_entry}" "{broken cache entry\n")
endforeach()
_run_codegen(miss)

if(NOT WIN32)
  # Cache writes are best effort: make a new key while the cache directory is
  # non-writable and require normal codegen success.
  gentest_fixture_write_file("${_private_header}" "#pragma once\ninline constexpr int private_value = 3;\n")
  file(CHMOD "${_cache_dir}" PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  _write_compdb(0)
  _run_codegen(miss)
  file(CHMOD "${_cache_dir}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

# The driver does not necessarily materialize CPATH in its cc1 argv. Its
# ordered raw value is therefore also context metadata: a newly configured
# ambient search root must not reuse an entry made without it.
set(_saved_cpath "$ENV{CPATH}")
unset(ENV{CPATH})
gentest_fixture_write_file("${_work_dir}/late/ambient.hpp" "#pragma once\ninline constexpr int ambient_value = 1;\n")
gentest_fixture_write_file("${_source}" [=[
#include "private.hpp"
#include "shared.hpp"
#include <shadow.hpp>
#include <ambient.hpp>

[[using gentest: test("cache/ambient")]]
void cache_ambient() {}
]=])
_run_codegen(miss)
_run_codegen(hit)
file(MAKE_DIRECTORY "${_work_dir}/cpath")
gentest_fixture_write_file("${_work_dir}/cpath/ambient.hpp" "#pragma once\ninline constexpr int ambient_value = 2;\n")
set(ENV{CPATH} "${_work_dir}/cpath")
_run_codegen(miss)
set(ENV{CPATH} "${_saved_cpath}")

# Direct-environment opt-in is separate from the directory setting: the CLI
# path wins even when the boolean environment switch says OFF, while ON plus
# _DIR enables a stable env-selected cache location.
set(_env_unused_cache "${_work_dir}/env-unused-cache")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE} "OFF")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE_DIR} "${_env_unused_cache}")
_run_codegen(hit)
if(EXISTS "${_env_unused_cache}")
  message(FATAL_ERROR "CLI --parse-cache-dir did not win over GENTEST_CODEGEN_PARSE_CACHE_DIR")
endif()

set(_env_cache_dir "${_work_dir}/env-cache")
set(_env_output_dir "${_work_dir}/env-generated")
file(MAKE_DIRECTORY "${_env_output_dir}")
function(_run_env_cache expected_cache)
  execute_process(
    COMMAND "${PROG}"
      --jobs=1
      --timing-json "${_env_output_dir}/timing.json"
      --tu-out-dir "${_env_output_dir}"
      --tu-header-output "${_env_output_dir}/cases.gentest.h"
      --compdb "${_work_dir}"
      "${_source}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "environment parse-cache invocation failed.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  file(READ "${_env_output_dir}/timing.json" _timing_text)
  string(REGEX MATCH "\\\"cache\\\"[ \t]*:[ \t]*\\\"${expected_cache}\\\"" _cache_match "${_timing_text}")
  if("${_cache_match}" STREQUAL "")
    message(FATAL_ERROR "Expected environment cache=${expected_cache}. Timing JSON:\n${_timing_text}")
  endif()
endfunction()
set(ENV{GENTEST_CODEGEN_PARSE_CACHE} "ON")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE_DIR} "${_env_cache_dir}")
_run_env_cache(miss)
_run_env_cache(hit)
if(NOT EXISTS "${_env_cache_dir}")
  message(FATAL_ERROR "GENTEST_CODEGEN_PARSE_CACHE=ON did not create its configured cache directory")
endif()

# An explicitly empty directory behaves like an unset directory: opt-in stays
# enabled and uses the documented compdb-relative default rather than silently
# constructing an empty cache path.
set(_empty_env_default_cache "${_work_dir}/.gentest_codegen_parse_cache")
file(REMOVE_RECURSE "${_empty_env_default_cache}")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE_DIR} "")
_run_env_cache(miss)
if(NOT EXISTS "${_empty_env_default_cache}")
  message(FATAL_ERROR "Empty GENTEST_CODEGEN_PARSE_CACHE_DIR did not select the default cache directory")
endif()
_run_env_cache(hit)

set(_invalid_cache_dir "${_work_dir}/invalid-env-cache")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE} "maybe")
set(ENV{GENTEST_CODEGEN_PARSE_CACHE_DIR} "${_invalid_cache_dir}")
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/invalid-env-generated"
    --tu-header-output "${_work_dir}/invalid-env-generated/cases.gentest.h"
    --compdb "${_work_dir}"
    "${_source}"
  RESULT_VARIABLE _invalid_rc
  OUTPUT_VARIABLE _invalid_out
  ERROR_VARIABLE _invalid_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _invalid_rc EQUAL 0)
  message(FATAL_ERROR "Invalid parse-cache boolean should leave codegen enabled.\n${_invalid_out}\n${_invalid_err}")
endif()
string(FIND "${_invalid_err}" "ignoring invalid GENTEST_CODEGEN_PARSE_CACHE='maybe'" _invalid_warning_pos)
if(_invalid_warning_pos EQUAL -1 OR EXISTS "${_invalid_cache_dir}")
  message(FATAL_ERROR "Invalid parse-cache boolean did not stay disabled with one warning.\n${_invalid_err}")
endif()
unset(ENV{GENTEST_CODEGEN_PARSE_CACHE})
unset(ENV{GENTEST_CODEGEN_PARSE_CACHE_DIR})
