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

include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

string(MD5 _search_roots_key "${_stage_dir}")
set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_SEARCH_ROOTS_${_search_roots_key}"
  "$<BUILD_INTERFACE:${_support_include_dir}>"
  "$<INSTALL_INTERFACE:include/fixture>")

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

message(STATUS "explicit mock target staging regression passed")
