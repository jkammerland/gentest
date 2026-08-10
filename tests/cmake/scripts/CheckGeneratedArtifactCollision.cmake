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

_gentest_expect_artifact_failure(
  "timing JSON/input source collision"
  ARGS
    --tu-out-dir "${_work_dir}/timing_source"
    --timing-json "${_source}"
    --compdb "${_build_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_source}"
  REQUIRED_SUBSTRINGS
    "generated artifact output at canonical path"
    "role 'timing JSON'"
    "input source")

set(_compile_commands "${_build_dir}/compile_commands.json")
_gentest_expect_artifact_failure(
  "timing JSON/compilation database collision"
  ARGS
    --tu-out-dir "${_work_dir}/timing_compdb"
    --timing-json "${_compile_commands}"
    --compdb "${_build_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_compile_commands}"
  REQUIRED_SUBSTRINGS
    "generated artifact output at canonical path"
    "role 'timing JSON'"
    "compilation database input")

set(_response_compdb_dir "${_work_dir}/response_compdb")
set(_response_file "${_response_compdb_dir}/flags.rsp")
file(MAKE_DIRECTORY "${_response_compdb_dir}")
file(WRITE "${_response_file}" "-DGENTEST_RESPONSE_FILE_SENTINEL=1\n")
file(READ "${_compile_commands}" _response_compdb_json)
string(JSON _response_command GET "${_response_compdb_json}" 0 command)
string(APPEND _response_command " @${_response_file}")
string(REPLACE "\\" "\\\\" _response_command_json "${_response_command}")
string(REPLACE "\"" "\\\"" _response_command_json "${_response_command_json}")
string(JSON _response_compdb_json SET "${_response_compdb_json}" 0 command "\"${_response_command_json}\"")
file(WRITE "${_response_compdb_dir}/compile_commands.json" "${_response_compdb_json}\n")
_gentest_expect_artifact_failure(
  "timing JSON/compilation response-file collision"
  ARGS
    --tu-out-dir "${_work_dir}/timing_response_file"
    --timing-json "${_response_file}"
    --compdb "${_response_compdb_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_response_file}"
  REQUIRED_SUBSTRINGS
    "compilation response-file input")

set(_source_root_input "${_work_dir}/source_root_input.txt")
file(WRITE "${_source_root_input}" "source-root timing collision sentinel\n")
_gentest_expect_artifact_failure(
  "timing JSON/source-root input collision"
  ARGS
    --check
    --source-root "${_source_root_input}"
    --timing-json "${_source_root_input}"
    --compdb "${_build_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_source_root_input}"
  REQUIRED_SUBSTRINGS
    "generated artifact output at canonical path"
    "role 'timing JSON'"
    "source root input")

set(_clang_scan_deps_input "${_work_dir}/clang_scan_deps_input")
file(WRITE "${_clang_scan_deps_input}" "clang-scan-deps timing collision sentinel\n")
_gentest_expect_artifact_failure(
  "timing JSON/explicit clang-scan-deps executable collision"
  ARGS
    --tu-out-dir "${_work_dir}/timing_scan_deps"
    --timing-json "${_clang_scan_deps_input}"
    --clang-scan-deps "${_clang_scan_deps_input}"
    --scan-deps-mode=OFF
    --compdb "${_build_dir}"
    "${_source}"
  PRESERVE_FILES
    "${_clang_scan_deps_input}"
  REQUIRED_SUBSTRINGS
    "resolved clang-scan-deps executable")

_gentest_expect_artifact_failure(
  "timing JSON/gentest_codegen executable collision"
  ARGS
    --tu-out-dir "${_work_dir}/timing_codegen_executable"
    --tu-header-output "${_work_dir}/timing_codegen_executable/tu_0.gentest.h"
    --timing-json "${PROG}"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_work_dir}/timing_codegen_executable/tu_0.gentest.h"
  PRESERVE_FILES
    "${PROG}"
  REQUIRED_SUBSTRINGS
    "gentest_codegen executable")

find_program(_timing_host_clang NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++ clang++.exe)
if(_timing_host_clang)
  set(_timing_host_compiler_header "${_work_dir}/timing_host_compiler/tu_0.gentest.h")
  _gentest_expect_artifact_failure(
    "timing JSON/resolved host compiler collision"
    ARGS
      --tu-out-dir "${_work_dir}/timing_host_compiler"
      --tu-header-output "${_timing_host_compiler_header}"
      --timing-json "${_timing_host_clang}"
      --host-clang "${_timing_host_clang}"
      --compdb "${_build_dir}"
      "${_source}"
    OUTPUTS
      "${_timing_host_compiler_header}"
    PRESERVE_FILES
      "${_timing_host_clang}"
    REQUIRED_SUBSTRINGS
      "resolved host compiler")
endif()

if(APPLE AND (NOT DEFINED ENV{SDKROOT} OR "$ENV{SDKROOT}" STREQUAL ""))
  find_program(_timing_xcrun NAMES xcrun)
  if(_timing_xcrun)
    _gentest_expect_artifact_failure(
      "timing JSON/resolved xcrun collision"
      ARGS
        --check
        --timing-json "${_timing_xcrun}"
        --compdb "${_build_dir}"
        "${_source}"
      PRESERVE_FILES
        "${_timing_xcrun}"
      REQUIRED_SUBSTRINGS
        "resolved xcrun executable")
  endif()
endif()

# Pass only the basename so the codegen resolver must find this executable via
# PATH. The raw option text differs from the timing target, exercising the
# late resolved-tool collision check rather than the static option-path check.
find_program(_timing_scan_deps NAMES clang-scan-deps-23 clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20 clang-scan-deps-19 clang-scan-deps)
if(_timing_scan_deps)
  get_filename_component(_timing_scan_deps_dir "${_timing_scan_deps}" DIRECTORY)
  get_filename_component(_timing_scan_deps_basename "${_timing_scan_deps}" NAME)
  if(CMAKE_HOST_WIN32)
    set(_timing_path_separator ";")
  else()
    set(_timing_path_separator ":")
  endif()
  set(_timing_scan_deps_path "${_timing_scan_deps_dir}${_timing_path_separator}$ENV{PATH}")
  set(_timing_scan_deps_header "${_work_dir}/timing_resolved_scan_deps/tu_0.gentest.h")
  set(_timing_scan_deps_target_args)
  if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
    list(APPEND _timing_scan_deps_target_args -- "${TARGET_ARG}")
  endif()
  file(SHA256 "${_timing_scan_deps}" _timing_scan_deps_hash_before)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env "PATH=${_timing_scan_deps_path}"
      "${PROG}"
      --tu-out-dir "${_work_dir}/timing_resolved_scan_deps"
      --tu-header-output "${_timing_scan_deps_header}"
      --timing-json "${_timing_scan_deps}"
      --clang-scan-deps "${_timing_scan_deps_basename}"
      --scan-deps-mode=AUTO
      --compdb "${_build_dir}"
      "${_source}"
      ${_timing_scan_deps_target_args}
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _timing_scan_deps_rc
    OUTPUT_VARIABLE _timing_scan_deps_out
    ERROR_VARIABLE _timing_scan_deps_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  set(_timing_scan_deps_all "${_timing_scan_deps_out}\n${_timing_scan_deps_err}")
  if(_timing_scan_deps_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected timing JSON/resolved clang-scan-deps collision to fail, but it succeeded. Output:\n${_timing_scan_deps_all}")
  endif()
  string(FIND "${_timing_scan_deps_all}" "resolved clang-scan-deps executable" _timing_scan_deps_error_pos)
  if(_timing_scan_deps_error_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected resolved clang-scan-deps timing collision diagnostic. Output:\n${_timing_scan_deps_all}")
  endif()
  if(EXISTS "${_timing_scan_deps_header}")
    message(FATAL_ERROR
      "Resolved clang-scan-deps timing collision created '${_timing_scan_deps_header}' before failing")
  endif()
  file(SHA256 "${_timing_scan_deps}" _timing_scan_deps_hash_after)
  if(NOT "${_timing_scan_deps_hash_after}" STREQUAL "${_timing_scan_deps_hash_before}")
    message(FATAL_ERROR
      "Resolved clang-scan-deps timing collision modified '${_timing_scan_deps}'")
  endif()

  set(_timing_scan_deps_off_header "${_work_dir}/timing_resolved_scan_deps_off/tu_0.gentest.h")
  file(SHA256 "${_timing_scan_deps}" _timing_scan_deps_off_hash_before)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env "PATH=${_timing_scan_deps_path}"
      "${PROG}"
      --tu-out-dir "${_work_dir}/timing_resolved_scan_deps_off"
      --tu-header-output "${_timing_scan_deps_off_header}"
      --timing-json "${_timing_scan_deps}"
      --clang-scan-deps "${_timing_scan_deps_basename}"
      --scan-deps-mode=OFF
      --compdb "${_build_dir}"
      "${_source}"
      ${_timing_scan_deps_target_args}
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _timing_scan_deps_off_rc
    OUTPUT_VARIABLE _timing_scan_deps_off_out
    ERROR_VARIABLE _timing_scan_deps_off_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  set(_timing_scan_deps_off_all "${_timing_scan_deps_off_out}\n${_timing_scan_deps_off_err}")
  if(_timing_scan_deps_off_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected OFF-mode timing JSON/resolved clang-scan-deps collision to fail, but it succeeded. Output:\n${_timing_scan_deps_off_all}")
  endif()
  string(FIND "${_timing_scan_deps_off_all}" "resolved clang-scan-deps executable" _timing_scan_deps_off_error_pos)
  if(_timing_scan_deps_off_error_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected OFF-mode resolved clang-scan-deps timing collision diagnostic. Output:\n${_timing_scan_deps_off_all}")
  endif()
  if(EXISTS "${_timing_scan_deps_off_header}")
    message(FATAL_ERROR
      "OFF-mode resolved clang-scan-deps timing collision created '${_timing_scan_deps_off_header}' before failing")
  endif()
  file(SHA256 "${_timing_scan_deps}" _timing_scan_deps_off_hash_after)
  if(NOT "${_timing_scan_deps_off_hash_after}" STREQUAL "${_timing_scan_deps_off_hash_before}")
    message(FATAL_ERROR
      "OFF-mode resolved clang-scan-deps timing collision modified '${_timing_scan_deps}'")
  endif()
endif()

set(_inspect_include_input "${_work_dir}/inspect_include_input.txt")
file(WRITE "${_inspect_include_input}" "inspect include timing collision sentinel\n")
_gentest_expect_artifact_failure(
  "inspect-source timing JSON/include-directory input collision"
  ARGS
    --inspect-source
    --inspect-include-dir "${_inspect_include_input}"
    --timing-json "${_inspect_include_input}"
    "${_source}"
  PRESERVE_FILES
    "${_inspect_include_input}"
  REQUIRED_SUBSTRINGS
    "inspect include directory")

_gentest_expect_artifact_failure(
  "inspect-source timing JSON/input source collision"
  ARGS
    --inspect-source
    --timing-json "${_source}"
    "${_source}"
  PRESERVE_FILES
    "${_source}"
  REQUIRED_SUBSTRINGS
    "generated artifact output at canonical path"
    "role 'timing JSON'"
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

set(_timing_header_dir "${_work_dir}/timing_header")
_gentest_expect_artifact_failure(
  "timing JSON/TU header collision"
  ARGS
    --tu-out-dir "${_timing_header_dir}"
    --tu-header-output "${_timing_header_dir}/Timing.JSON"
    --timing-json "${_timing_header_dir}/timing.json"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_timing_header_dir}"
    "${_timing_header_dir}/Timing.JSON"
    "${_timing_header_dir}/timing.json"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'TU registration header'"
    "role 'timing JSON'")

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

set(_timing_manifest_dir "${_work_dir}/timing_manifest")
_gentest_expect_artifact_failure(
  "timing JSON/artifact manifest collision"
  ARGS
    --tu-out-dir "${_timing_manifest_dir}"
    --artifact-manifest "${_timing_manifest_dir}/Artifacts.JSON"
    --timing-json "${_timing_manifest_dir}/artifacts.json"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_timing_manifest_dir}"
    "${_timing_manifest_dir}/Artifacts.JSON"
    "${_timing_manifest_dir}/artifacts.json"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'artifact manifest'"
    "role 'timing JSON'")

set(_timing_mock_dir "${_work_dir}/timing_mock")
_gentest_expect_artifact_failure(
  "timing JSON/mock output collision"
  ARGS
    --tu-out-dir "${_timing_mock_dir}"
    --mock-registry "${_timing_mock_dir}/Mock.JSON"
    --mock-impl "${_timing_mock_dir}/mock_impl.hpp"
    --mock-domain-registry-output "${_timing_mock_dir}/domain_registry.hpp"
    --mock-domain-impl-output "${_timing_mock_dir}/domain_impl.hpp"
    --timing-json "${_timing_mock_dir}/mock.json"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_timing_mock_dir}"
    "${_timing_mock_dir}/Mock.JSON"
    "${_timing_mock_dir}/mock.json"
    "${_timing_mock_dir}/mock_impl.hpp"
    "${_timing_mock_dir}/domain_registry.hpp"
    "${_timing_mock_dir}/domain_impl.hpp"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'mock registry'"
    "role 'timing JSON'")

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

set(_timing_depfile_dir "${_work_dir}/timing_depfile")
_gentest_expect_artifact_failure(
  "timing JSON/depfile collision"
  ARGS
    --tu-out-dir "${_timing_depfile_dir}"
    --depfile "${_timing_depfile_dir}/Deps.JSON"
    --timing-json "${_timing_depfile_dir}/deps.json"
    --compdb "${_build_dir}"
    "${_source}"
  OUTPUTS
    "${_timing_depfile_dir}"
    "${_timing_depfile_dir}/Deps.JSON"
    "${_timing_depfile_dir}/deps.json"
  REQUIRED_SUBSTRINGS
    "generated artifact collision at canonical path"
    "role 'depfile'"
    "role 'timing JSON'")

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
