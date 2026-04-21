if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenResponseFileExpansion.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenResponseFileExpansion.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenResponseFileExpansion.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenResponseFileExpansion.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

find_program(_real_clang NAMES clang++-22 clang++-21 clang++-20 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)

set(_work_dir "${BUILD_ROOT}/codegen_response_file_expansion")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_fixture_source "${SOURCE_DIR}/tests/smoke/namespaced_attrs.cpp")
if(NOT EXISTS "${_fixture_source}")
  message(FATAL_ERROR "CheckCodegenResponseFileExpansion.cmake: missing fixture source '${_fixture_source}'")
endif()

file(COPY "${_fixture_source}" DESTINATION "${_work_dir}")
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
file(TO_CMAKE_PATH "${_work_dir}/namespaced_attrs.cpp" _source_abs)
file(TO_CMAKE_PATH "${_work_dir}/namespaced_attrs.gentest.h" _output_abs)
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${_source_dir_norm}"
  APPLE_SYSROOT)
gentest_normalize_std_flag_for_compiler(_compdb_std "${_real_clang_norm}" "${CODEGEN_STD}")
gentest_normalize_include_args_for_compiler(_compdb_include_args "${_real_clang_norm}" ${_public_include_args})
string(JOIN "\n" _public_include_args_rsp ${_compdb_include_args})

gentest_fixture_write_file("${_work_dir}/args.rsp" "
${_compdb_std}
${_public_include_args_rsp}
-c
${_source_abs}
-o
namespaced_attrs.o
&&
${CMAKE_COMMAND}
-E
echo
response-tail-should-not-run
")
gentest_fixture_write_file("${_work_dir}/empty.rsp" "")

set(_command_args "${_real_clang_norm}")
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _command_args "${TARGET_ARG}")
endif()
list(APPEND _command_args "@args.rsp" "@empty.rsp" "@missing.rsp")

gentest_fixture_make_compdb_entry(_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_source_abs}"
  ARGUMENTS ${_command_args})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_entry}")

execute_process(
  COMMAND
    "${PROG}"
    --tu-out-dir "${_work_dir}"
    --tu-header-output "${_output_abs}"
    --compdb "${_work_dir}"
    "${_source_abs}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "response-file compile command expansion failed.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

if(NOT EXISTS "${_output_abs}")
  message(FATAL_ERROR "Expected generated output '${_output_abs}' to exist")
endif()

file(READ "${_output_abs}" _generated_text)
string(FIND "${_generated_text}" "smoke/namespaced/first" _case_pos)
if(_case_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected generated output '${_output_abs}' to contain the response-file smoke registration.\n"
    "${_generated_text}")
endif()

string(FIND "${_err}" "response-tail-should-not-run" _tail_pos)
if(NOT _tail_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected shell tail embedded in the response file to be stripped.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()
