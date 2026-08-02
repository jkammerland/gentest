# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<CMake generator name>
#  -DPROG=<path to gentest_codegen executable>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DTARGET_ARG=<optional --target=... argument>

foreach(_required IN ITEMS SOURCE_DIR BUILD_ROOT GENERATOR PROG)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckGeneratedArtifactCollision.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/generated_artifact_collision")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")
set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure generated-artifact collision fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_build_cmd
  "${CMAKE_COMMAND}"
  --build "${_build_dir}"
  --target generated_artifact_collision_obj)
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_TYPE}")
endif()

message(STATUS "Build generated-artifact collision fixture target...")
gentest_check_run_or_fail(
  COMMAND ${_build_cmd}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_source "${SOURCE_DIR}/case.cpp")
if(NOT EXISTS "${_build_dir}/compile_commands.json")
  message(FATAL_ERROR "Expected fixture compile database '${_build_dir}/compile_commands.json'")
endif()

function(_gentest_expect_artifact_failure name)
  set(multi_value_args ARGS OUTPUTS PRESERVE_FILES REQUIRED_SUBSTRINGS)
  cmake_parse_arguments(CASE "" "" "${multi_value_args}" ${ARGN})

  set(_command "${PROG}" ${CASE_ARGS})
  if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
    list(APPEND _command -- "${TARGET_ARG}")
  endif()

  set(_preserve_hashes "")
  foreach(_preserve IN LISTS CASE_PRESERVE_FILES)
    if(NOT EXISTS "${_preserve}")
      message(FATAL_ERROR "${name}: input '${_preserve}' does not exist before validation")
    endif()
    file(SHA256 "${_preserve}" _preserve_before)
    list(APPEND _preserve_hashes "${_preserve_before}")
  endforeach()

  execute_process(
    COMMAND ${_command}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${_work_dir}"
  )

  set(_all "${_out}\n${_err}")
  if(_rc EQUAL 0)
    message(FATAL_ERROR "Expected ${name} to fail, but it succeeded. Output:\n${_all}")
  endif()
  foreach(_required IN LISTS CASE_REQUIRED_SUBSTRINGS)
    string(FIND "${_all}" "${_required}" _required_pos)
    if(_required_pos EQUAL -1)
      message(FATAL_ERROR "${name}: expected diagnostic '${_required}'. Output:\n${_all}")
    endif()
  endforeach()
  foreach(_output IN LISTS CASE_OUTPUTS)
    if(EXISTS "${_output}")
      message(FATAL_ERROR "${name}: collision validation created output '${_output}' before failing. Output:\n${_all}")
    endif()
  endforeach()
  list(LENGTH CASE_PRESERVE_FILES _preserve_count)
  if(_preserve_count GREATER 0)
    math(EXPR _preserve_last "${_preserve_count} - 1")
    foreach(_preserve_idx RANGE 0 ${_preserve_last})
      list(GET CASE_PRESERVE_FILES ${_preserve_idx} _preserve)
      list(GET _preserve_hashes ${_preserve_idx} _preserve_before)
      if(NOT EXISTS "${_preserve}")
        message(FATAL_ERROR "${name}: expected input '${_preserve}' to remain after validation. Output:\n${_all}")
      endif()
      file(SHA256 "${_preserve}" _preserve_after)
      if(NOT "${_preserve_after}" STREQUAL "${_preserve_before}")
        message(FATAL_ERROR "${name}: collision validation modified input '${_preserve}'. Output:\n${_all}")
      endif()
    endforeach()
  endif()
endfunction()

_gentest_expect_artifact_failure(
  "TU header/input source collision"
  ARGS
    --tu-out-dir "${_work_dir}/source_input"
    --tu-header-output "${_source}"
    --compdb "${_build_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_source}"
  REQUIRED_SUBSTRINGS
    "generated artifact output at canonical path"
    "role 'TU registration header'"
    "input source")

set(_tu_registry_dir "${_work_dir}/tu_registry")
_gentest_expect_artifact_failure(
  "TU header/mock registry collision"
  ARGS
    --tu-out-dir "${_tu_registry_dir}"
    --tu-header-output "${_tu_registry_dir}/Collision.hpp"
    --mock-registry "${_tu_registry_dir}/collision.HPP"
    --mock-impl "${_tu_registry_dir}/mock_impl.hpp"
    --mock-domain-registry-output "${_tu_registry_dir}/domain_registry.hpp"
    --mock-domain-impl-output "${_tu_registry_dir}/domain_impl.hpp"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_tu_registry_dir}"
    "${_tu_registry_dir}/Collision.hpp"
    "${_tu_registry_dir}/collision.HPP"
    "${_tu_registry_dir}/mock_impl.hpp"
    "${_tu_registry_dir}/domain_registry.hpp"
    "${_tu_registry_dir}/domain_impl.hpp"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'TU registration header'"
    "role 'mock registry'"
    "source slot 0"
    "option '--mock-registry'"
)

set(_manifest_impl_dir "${_work_dir}/manifest_impl")
_gentest_expect_artifact_failure(
  "artifact manifest/mock implementation collision"
  ARGS
    --tu-out-dir "${_manifest_impl_dir}"
    --tu-header-output "${_manifest_impl_dir}/case.gentest.h"
    --artifact-manifest "${_manifest_impl_dir}/Artifacts.JSON"
    --mock-registry "${_manifest_impl_dir}/mock_registry.hpp"
    --mock-impl "${_manifest_impl_dir}/artifacts.json"
    --mock-domain-registry-output "${_manifest_impl_dir}/domain_registry.hpp"
    --mock-domain-impl-output "${_manifest_impl_dir}/domain_impl.hpp"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_manifest_impl_dir}"
    "${_manifest_impl_dir}/case.gentest.h"
    "${_manifest_impl_dir}/Artifacts.JSON"
    "${_manifest_impl_dir}/mock_registry.hpp"
    "${_manifest_impl_dir}/artifacts.json"
    "${_manifest_impl_dir}/domain_registry.hpp"
    "${_manifest_impl_dir}/domain_impl.hpp"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'artifact manifest'"
    "role 'mock implementation'"
    "option '--artifact-manifest'"
    "option '--mock-impl'"
)

set(_depfile_dir "${_work_dir}/depfile")
_gentest_expect_artifact_failure(
  "TU header/depfile collision"
  ARGS
    --tu-out-dir "${_depfile_dir}"
    --tu-header-output "${_depfile_dir}/Case.gentest.h"
    --depfile "${_depfile_dir}/case.GENTEST.H"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_depfile_dir}"
    "${_depfile_dir}/Case.gentest.h"
    "${_depfile_dir}/case.GENTEST.H"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'TU registration header'"
    "role 'depfile'"
    "option '--depfile'"
)

set(_mock_domain_dir "${_work_dir}/mock_domain")
_gentest_expect_artifact_failure(
  "explicit mock domain collision"
  ARGS
    --tu-out-dir "${_mock_domain_dir}"
    --tu-header-output "${_mock_domain_dir}/case.gentest.h"
    --mock-registry "${_mock_domain_dir}/mock_registry.hpp"
    --mock-impl "${_mock_domain_dir}/mock_impl.hpp"
    --mock-domain-registry-output "${_mock_domain_dir}/Domain.hpp"
    --mock-domain-impl-output "${_mock_domain_dir}/domain.HPP"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_mock_domain_dir}"
    "${_mock_domain_dir}/case.gentest.h"
    "${_mock_domain_dir}/mock_registry.hpp"
    "${_mock_domain_dir}/mock_impl.hpp"
    "${_mock_domain_dir}/Domain.hpp"
    "${_mock_domain_dir}/domain.HPP"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'mock domain registry'"
    "role 'mock domain implementation'"
    "mock output domain slot 0 'header'"
)

set(_empty_slot_dir "${_work_dir}/empty_slot")
_gentest_expect_artifact_failure(
  "empty textual wrapper output slot"
  ARGS
    --tu-out-dir "${_empty_slot_dir}"
    --tu-header-output "${_empty_slot_dir}/case.gentest.h"
    "--textual-wrapper-output="
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_empty_slot_dir}"
    "${_empty_slot_dir}/case.gentest.h"
  REQUIRED_SUBSTRINGS
    "generated artifact output is empty for role 'textual wrapper'"
    "source slot 0"
)

# An empty TU-header slot intentionally selects the derived header path.
set(_derived_dir "${_work_dir}/derived_header")
file(SHA256 "${_source}" _source_hash_before)
set(_derived_command
  "${PROG}"
  --tu-out-dir "${_derived_dir}"
  "--tu-header-output="
  --compdb "${_build_dir}"
  "${_source}")
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _derived_command -- "${TARGET_ARG}")
endif()
gentest_check_run_or_fail(
  COMMAND ${_derived_command}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)
if(NOT EXISTS "${_derived_dir}/case.h")
  message(FATAL_ERROR "Expected empty --tu-header-output slot to emit derived header '${_derived_dir}/case.h'")
endif()
file(SHA256 "${_source}" _source_hash_after)
if(NOT "${_source_hash_after}" STREQUAL "${_source_hash_before}")
  message(FATAL_ERROR "Source-associated module-wrapper slot modified its owner source '${_source}'")
endif()

message(STATUS "Generated-artifact collision regression passed")
