# Requires:
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetStaging.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetStaging.cmake: GENTEST_SOURCE_DIR not set")
endif()

set(_work_dir "${BUILD_ROOT}/explicit_mock_target_staging")
set(_stage_dir "${_work_dir}/stage")
set(_src_dir "${_work_dir}/src")
set(_support_include_dir "${_work_dir}/support/include")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_stage_dir}" "${_src_dir}" "${_support_include_dir}/fixture")

file(WRITE "${_support_include_dir}/fixture/support.hpp"
"#pragma once\n\
namespace fixture {\n\
inline constexpr int kSupportSentinel = 42;\n\
} // namespace fixture\n")

file(WRITE "${_src_dir}/shared_defs.hpp"
"#pragma once\n\
#include \"fixture/support.hpp\"\n\
namespace fixture::mocks {\n\
inline constexpr int kMirroredSentinel = fixture::kSupportSentinel;\n\
} // namespace fixture::mocks\n")

file(WRITE "${_src_dir}/aggregate_defs.hpp"
"#pragma once\n\
#include \"shared_defs.hpp\"\n\
namespace fixture::mocks {\n\
using SharedDefsTag = int;\n\
} // namespace fixture::mocks\n")

file(WRITE "${_src_dir}/third_party_dependency.hpp"
"#pragma once\n\
#include <gentest/mock.h>\n")

file(WRITE "${_src_dir}/third_party_defs.hpp"
"#pragma once\n\
#include \"third_party_dependency.hpp\"\n\
namespace fixture::mocks {\n\
using ThirdPartyTag = int;\n\
} // namespace fixture::mocks\n")

include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

string(MD5 _search_roots_key "${_stage_dir}")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_SEARCH_ROOTS_${_search_roots_key}"
  "$<BUILD_INTERFACE:${_support_include_dir}>"
  "$<INSTALL_INTERFACE:include/fixture>")

function(_gentest_timestamp path out_var)
  file(TIMESTAMP "${path}" _timestamp "%s.%f" UTC)
  if("${_timestamp}" STREQUAL "")
    message(FATAL_ERROR "Unable to read timestamp for '${path}'")
  endif()
  set(${out_var} "${_timestamp}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_same_timestamp path expected label)
  _gentest_timestamp("${path}" _actual)
  if(NOT "${_actual}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "${label}: expected '${path}' to retain timestamp '${expected}', got '${_actual}'")
  endif()
endfunction()

_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/shared_defs.hpp"
  "shared_defs.hpp"
  _direct_staged_files)

_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/aggregate_defs.hpp"
  "aggregate_defs.hpp"
  _aggregate_staged_files)

# Missing outputs are created, while equal content retains the exact mtime.
# The space-containing path exercises CMake argument handling as well.
set(_atomic_output "${_stage_dir}/path with spaces/atomic.hpp")
if(EXISTS "${_atomic_output}")
  message(FATAL_ERROR "Expected atomic staging probe to start missing: ${_atomic_output}")
endif()
_gentest_write_file_atomic_if_changed("${_atomic_output}" "first\n")
if(NOT EXISTS "${_atomic_output}")
  message(FATAL_ERROR "Atomic staging probe was not created: ${_atomic_output}")
endif()
_gentest_timestamp("${_atomic_output}" _atomic_initial_timestamp)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
_gentest_write_file_atomic_if_changed("${_atomic_output}" "first\n")
_gentest_expect_same_timestamp("${_atomic_output}" "${_atomic_initial_timestamp}" "unchanged atomic staging probe")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
_gentest_write_file_atomic_if_changed("${_atomic_output}" "second\n")
_gentest_timestamp("${_atomic_output}" _atomic_changed_timestamp)
if("${_atomic_changed_timestamp}" STREQUAL "${_atomic_initial_timestamp}")
  message(FATAL_ERROR "Changed atomic staging probe did not receive a new timestamp: ${_atomic_output}")
endif()

set(_direct_shared_stage "${_stage_dir}/shared_defs.hpp")
if(NOT EXISTS "${_direct_shared_stage}")
  message(FATAL_ERROR "Expected direct staged defs file was not written: ${_direct_shared_stage}")
endif()

file(GLOB _dep_shared_stage "${_stage_dir}/deps/*_shared_defs.hpp")
list(LENGTH _dep_shared_stage _dep_shared_stage_count)
if(NOT _dep_shared_stage_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one staged dependency copy of shared_defs.hpp under '${_stage_dir}/deps', found ${_dep_shared_stage_count}: "
    "${_dep_shared_stage}")
endif()

file(GLOB _dep_support_stage "${_stage_dir}/deps/*_support.hpp")
list(LENGTH _dep_support_stage _dep_support_stage_count)
if(NOT _dep_support_stage_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one staged dependency copy of fixture/support.hpp under '${_stage_dir}/deps', found ${_dep_support_stage_count}: "
    "${_dep_support_stage}")
endif()

file(READ "${_stage_dir}/aggregate_defs.hpp" _aggregate_staged_content)
if(NOT _aggregate_staged_content MATCHES "#include \"deps/[^\"]+_shared_defs\\.hpp\"")
  message(FATAL_ERROR
    "Expected aggregate staged defs to rewrite shared_defs.hpp into a staged deps include. Content:\n${_aggregate_staged_content}")
endif()

file(READ "${_dep_shared_stage}" _dep_shared_content)
if(NOT _dep_shared_content MATCHES "#include \"[^\"]+_support\\.hpp\"")
  message(FATAL_ERROR
    "Expected shared_defs staged dependency copy to rewrite fixture/support.hpp via BUILD_INTERFACE roots. Content:\n${_dep_shared_content}")
endif()

# A new configure has no in-process staging cache. Clear the direct-file cache
# entry to exercise the same write path without changing the source.
_gentest_timestamp("${_direct_shared_stage}" _direct_shared_timestamp)
string(MD5 _direct_shared_stage_key "${_stage_dir}|${_src_dir}/shared_defs.hpp|shared_defs.hpp")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_STAGE_${_direct_shared_stage_key}" FALSE)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/shared_defs.hpp"
  "shared_defs.hpp"
  _restaged_direct_files)
_gentest_expect_same_timestamp("${_direct_shared_stage}" "${_direct_shared_timestamp}" "unchanged staged defs")

# Third-party definitions and their recursively staged dependencies must be
# rewritten before their sole publication, so repeat staging remains a no-op.
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_REWRITE_THIRD_PARTY_API_${_search_roots_key}" TRUE)
_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/third_party_defs.hpp"
  "third_party_defs.hpp"
  _third_party_staged_files)
set(_third_party_staged "${_stage_dir}/third_party_defs.hpp")
file(GLOB _third_party_dependency_staged "${_stage_dir}/deps/*_third_party_dependency.hpp")
list(LENGTH _third_party_dependency_staged _third_party_dependency_count)
if(NOT _third_party_dependency_count EQUAL 1)
  message(FATAL_ERROR
    "Expected one staged third-party dependency, found ${_third_party_dependency_count}: ${_third_party_dependency_staged}")
endif()
file(READ "${_third_party_dependency_staged}" _third_party_dependency_content)
if(NOT _third_party_dependency_content MATCHES "#include[ \t]+\"gentest/mock_fwd\\.h\"")
  message(FATAL_ERROR
    "Expected recursive third-party staged dependency to rewrite gentest/mock.h before publishing. Content:\n${_third_party_dependency_content}")
endif()
_gentest_timestamp("${_third_party_staged}" _third_party_timestamp)
_gentest_timestamp("${_third_party_dependency_staged}" _third_party_dependency_timestamp)
string(MD5 _third_party_stage_key "${_stage_dir}|${_src_dir}/third_party_defs.hpp|third_party_defs.hpp")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_STAGE_${_third_party_stage_key}" FALSE)
string(MD5 _third_party_dependency_hash "${_src_dir}/third_party_dependency.hpp")
set(_third_party_dependency_rel "deps/${_third_party_dependency_hash}_third_party_dependency.hpp")
string(MD5 _third_party_dependency_stage_key
  "${_stage_dir}|${_src_dir}/third_party_dependency.hpp|${_third_party_dependency_rel}")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_STAGE_${_third_party_dependency_stage_key}" FALSE)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/third_party_defs.hpp"
  "third_party_defs.hpp"
  _restaged_third_party_files)
_gentest_expect_same_timestamp("${_third_party_staged}" "${_third_party_timestamp}" "unchanged third-party staged defs")
_gentest_expect_same_timestamp("${_third_party_dependency_staged}" "${_third_party_dependency_timestamp}"
  "unchanged recursive third-party staged dependency")

# A real source change must still update the staged artifact.
file(READ "${_src_dir}/shared_defs.hpp" _shared_defs_source)
string(APPEND _shared_defs_source "\n// changed staged source\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${_src_dir}/shared_defs.hpp" "${_shared_defs_source}")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_STAGE_${_direct_shared_stage_key}" FALSE)
_gentest_stage_explicit_mock_file(
  "${_stage_dir}"
  "${_src_dir}/shared_defs.hpp"
  "shared_defs.hpp"
  _changed_direct_files)
_gentest_timestamp("${_direct_shared_stage}" _changed_direct_timestamp)
if("${_changed_direct_timestamp}" STREQUAL "${_direct_shared_timestamp}")
  message(FATAL_ERROR "Changed staged defs did not receive a new timestamp: ${_direct_shared_stage}")
endif()
file(READ "${_direct_shared_stage}" _changed_direct_content)
if(NOT _changed_direct_content MATCHES "changed staged source")
  message(FATAL_ERROR "Changed staged defs content was not published: ${_direct_shared_stage}")
endif()

message(STATUS "explicit mock target staging regression passed")
