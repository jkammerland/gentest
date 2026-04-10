if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSyntheticFallback.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSyntheticFallback.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSyntheticFallback.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSyntheticFallback.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/codegen_synthetic_compdb_fallback")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_fixture_source "${SOURCE_DIR}/tests/smoke/namespaced_attrs.cpp")
if(NOT EXISTS "${_fixture_source}")
  message(FATAL_ERROR "CheckCodegenSyntheticFallback.cmake: missing fixture source '${_fixture_source}'")
endif()

file(COPY "${_fixture_source}" DESTINATION "${_work_dir}")
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json")

file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}/namespaced_attrs.cpp" _source_norm)
file(TO_CMAKE_PATH "${_work_dir}/namespaced_attrs.gentest.cpp" _output_norm)

set(_codegen_args
  --output "${_output_norm}"
  --compdb "${_work_dir_norm}"
  "${_source_norm}"
  --)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _codegen_args "${TARGET_ARG}")
endif()
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${_source_dir_norm}"
  APPLE_SYSROOT)
gentest_normalize_std_flag_for_compiler(_synthetic_std "clang++" "${CODEGEN_STD}")
list(APPEND _codegen_args "${_synthetic_std}" ${_public_include_args})

execute_process(
  COMMAND
    "${PROG}"
    ${_codegen_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "gentest_codegen failed for the synthetic compdb fallback path.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

set(_expected_warning
  "gentest_codegen: warning: no compilation database entry for '${_source_norm}'; using synthetic clang invocation (compdb: '${_work_dir_norm}')")
string(FIND "${_err}" "${_expected_warning}" _warning_pos)
if(_warning_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected synthetic compdb fallback warning.\n"
    "Needle: ${_expected_warning}\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

if(NOT EXISTS "${_output_norm}")
  message(FATAL_ERROR "Expected generated output '${_output_norm}' to exist")
endif()

file(READ "${_output_norm}" _generated_text)
string(FIND "${_generated_text}" "smoke/namespaced/first" _case_pos)
if(_case_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected generated output '${_output_norm}' to contain the synthetic-fallback case registration.\n"
    "${_generated_text}")
endif()
