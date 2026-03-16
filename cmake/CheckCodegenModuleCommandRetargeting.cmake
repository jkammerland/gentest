# Requires:
#  -DPROG=<path to gentest_codegen executable>
#  -DBUILD_ROOT=<path to parent build dir>
# Optional:
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenModuleCommandRetargeting.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenModuleCommandRetargeting.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if("${_clangxx}" STREQUAL "")
  gentest_skip_test("module command retargeting regression: no clang++ compiler available")
  return()
endif()

file(TO_CMAKE_PATH "${_clangxx}" _clangxx_norm)

function(_gentest_json_escape out_var value)
  set(_escaped "${value}")
  string(REPLACE "\\" "\\\\" _escaped "${_escaped}")
  string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
  string(REPLACE "\n" "\\n" _escaped "${_escaped}")
  string(REPLACE "\r" "\\r" _escaped "${_escaped}")
  string(REPLACE "\t" "\\t" _escaped "${_escaped}")
  set(${out_var} "${_escaped}" PARENT_SCOPE)
endfunction()

function(_gentest_json_array out_var)
  set(_json "[")
  set(_first TRUE)
  foreach(_item IN LISTS ARGN)
    _gentest_json_escape(_escaped "${_item}")
    if(NOT _first)
      string(APPEND _json ", ")
    endif()
    string(APPEND _json "\"${_escaped}\"")
    set(_first FALSE)
  endforeach()
  string(APPEND _json "]")
  set(${out_var} "${_json}" PARENT_SCOPE)
endfunction()

function(_gentest_write_file path)
  set(_content "")
  math(EXPR _last_arg "${ARGC} - 1")
  foreach(_idx RANGE 1 ${_last_arg})
    string(APPEND _content "${ARGV${_idx}}")
  endforeach()
  file(WRITE "${path}" "${_content}")
endfunction()

function(_gentest_write_single_entry_compdb path)
  set(one_value_args DIRECTORY FILE COMMAND)
  set(multi_value_args ARGUMENTS)
  cmake_parse_arguments(COMP "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT COMP_DIRECTORY OR NOT COMP_FILE)
    message(FATAL_ERROR "_gentest_write_single_entry_compdb requires DIRECTORY and FILE")
  endif()

  _gentest_json_escape(_dir_json "${COMP_DIRECTORY}")
  _gentest_json_escape(_file_json "${COMP_FILE}")

  if(DEFINED COMP_COMMAND AND NOT "${COMP_COMMAND}" STREQUAL "")
    _gentest_json_escape(_command_json "${COMP_COMMAND}")
    set(_entry_value "\"command\": \"${_command_json}\"")
  elseif(COMP_ARGUMENTS)
    _gentest_json_array(_arguments_json ${COMP_ARGUMENTS})
    set(_entry_value "\"arguments\": ${_arguments_json}")
  else()
    message(FATAL_ERROR "_gentest_write_single_entry_compdb requires COMMAND or ARGUMENTS")
  endif()

  _gentest_write_file("${path}"
    "[\n"
    "  {\n"
    "    \"directory\": \"${_dir_json}\",\n"
    "    \"file\": \"${_file_json}\",\n"
    "    ${_entry_value}\n"
    "  }\n"
    "]\n")
endfunction()

function(_gentest_run_codegen_expect_success)
  set(one_value_args NAME COMPDB_DIR TU_OUT_DIR SOURCE_FILE)
  cmake_parse_arguments(RUN "" "${one_value_args}" "" ${ARGN})

  if(NOT RUN_NAME OR NOT RUN_COMPDB_DIR OR NOT RUN_SOURCE_FILE)
    message(FATAL_ERROR "_gentest_run_codegen_expect_success requires NAME, COMPDB_DIR, and SOURCE_FILE")
  endif()

  set(_cmd "${PROG}" --check --compdb "${RUN_COMPDB_DIR}")
  if(DEFINED RUN_TU_OUT_DIR AND NOT "${RUN_TU_OUT_DIR}" STREQUAL "")
    list(APPEND _cmd --tu-out-dir "${RUN_TU_OUT_DIR}")
  endif()
  list(APPEND _cmd "${RUN_SOURCE_FILE}")

  execute_process(
    COMMAND ${_cmd}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "${RUN_NAME}: gentest_codegen failed unexpectedly.\n"
      "Output:\n${_out}\nErrors:\n${_err}")
  endif()
endfunction()

set(_work_dir "${BUILD_ROOT}/codegen_module_command_retargeting")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_relative_dir "${_work_dir}/relative")
file(MAKE_DIRECTORY "${_relative_dir}")
_gentest_write_file("${_relative_dir}/provider.cppm" [[
export module gentest.retarget.relative;
export int provider_value() { return 7; }
]])
file(TO_CMAKE_PATH "${_relative_dir}" _relative_dir_norm)
file(TO_CMAKE_PATH "${_relative_dir}/provider.cppm" _relative_source_abs)
_gentest_write_single_entry_compdb("${_relative_dir}/compile_commands.json"
  DIRECTORY "${_relative_dir_norm}"
  FILE "provider.cppm"
  ARGUMENTS "${_clangxx_norm}" "-std=c++20" "-c" "provider.cppm" "-o" "provider.o")

_gentest_run_codegen_expect_success(
  NAME "relative source argument"
  COMPDB_DIR "${_relative_dir}"
  SOURCE_FILE "${_relative_source_abs}")

set(_relative_include_dir "${_work_dir}/relative_include")
file(MAKE_DIRECTORY "${_relative_include_dir}/include")
_gentest_write_file("${_relative_include_dir}/include/value.hpp" [[
inline int relative_include_value() { return 11; }
]])
_gentest_write_file("${_relative_include_dir}/provider.cppm" [[
module;
#include "value.hpp"
export module gentest.retarget.relative_include;
export int provider_value() { return relative_include_value(); }
]])
file(TO_CMAKE_PATH "${_relative_include_dir}" _relative_include_dir_norm)
file(TO_CMAKE_PATH "${_relative_include_dir}/provider.cppm" _relative_include_source_abs)
_gentest_write_single_entry_compdb("${_relative_include_dir}/compile_commands.json"
  DIRECTORY "${_relative_include_dir_norm}"
  FILE "provider.cppm"
  ARGUMENTS "${_clangxx_norm}" "-std=c++20" "-Iinclude" "-c" "provider.cppm" "-o" "provider.o")

_gentest_run_codegen_expect_success(
  NAME "relative include path under compile-command cwd"
  COMPDB_DIR "${_relative_include_dir}"
  SOURCE_FILE "${_relative_include_source_abs}")

set(_response_dir "${_work_dir}/response_file")
set(_response_generated_dir "${_response_dir}/generated")
file(MAKE_DIRECTORY "${_response_generated_dir}")
_gentest_write_file("${_response_dir}/provider.cppm" [[
export module gentest.retarget.response;
export int response_value() { return 9; }
]])
file(TO_CMAKE_PATH "${_response_dir}" _response_dir_norm)
file(TO_CMAKE_PATH "${_response_dir}/provider.cppm" _response_source_abs)
file(TO_CMAKE_PATH "${_response_generated_dir}/tu_0000_provider.module.gentest.cppm" _wrapper_abs)
_gentest_write_file("${_response_generated_dir}/tu_0000_provider.module.gentest.cppm" [[
#error gentest wrapper retargeting regression: the generated wrapper should not be parsed directly
]])
_gentest_write_file("${_response_dir}/args.rsp" [[
-std=c++20
-c
generated/tu_0000_provider.module.gentest.cppm
-o
generated/tu_0000_provider.module.gentest.cppm.o
]])
_gentest_write_single_entry_compdb("${_response_dir}/compile_commands.json"
  DIRECTORY "${_response_dir_norm}"
  FILE "${_wrapper_abs}"
  ARGUMENTS "${_clangxx_norm}" "@args.rsp")

_gentest_run_codegen_expect_success(
  NAME "response-file wrapper retarget"
  COMPDB_DIR "${_response_dir}"
  TU_OUT_DIR "${_response_generated_dir}"
  SOURCE_FILE "${_response_source_abs}")

set(_joined_dep_dir "${_work_dir}/joined_depflags")
file(MAKE_DIRECTORY "${_joined_dep_dir}")
_gentest_write_file("${_joined_dep_dir}/provider.cppm" [[
export module gentest.retarget.joined_depflags;
export int joined_depflags_value() { return 13; }
]])
file(TO_CMAKE_PATH "${_joined_dep_dir}" _joined_dep_dir_norm)
file(TO_CMAKE_PATH "${_joined_dep_dir}/provider.cppm" _joined_dep_source_abs)
_gentest_write_single_entry_compdb("${_joined_dep_dir}/compile_commands.json"
  DIRECTORY "${_joined_dep_dir_norm}"
  FILE "provider.cppm"
  ARGUMENTS
    "${_clangxx_norm}"
    "-std=c++20"
    "-MF${_joined_dep_dir_norm}/deps.d"
    "-MT${_joined_dep_dir_norm}/provider.o"
    "-MQ${_joined_dep_dir_norm}/provider.o"
    "-c"
    "provider.cppm"
    "-o"
    "provider.o")

execute_process(
  COMMAND "${PROG}" --check --compdb "${_joined_dep_dir}" "${_joined_dep_source_abs}"
  RESULT_VARIABLE _joined_dep_rc
  OUTPUT_VARIABLE _joined_dep_out
  ERROR_VARIABLE _joined_dep_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _joined_dep_rc EQUAL 0)
  message(FATAL_ERROR
    "joined depfile flags: gentest_codegen failed unexpectedly.\n"
    "Output:\n${_joined_dep_out}\nErrors:\n${_joined_dep_err}")
endif()
if(_joined_dep_err MATCHES "argument unused during compilation: '-MF"
   OR _joined_dep_err MATCHES "argument unused during compilation: '-MT"
   OR _joined_dep_err MATCHES "argument unused during compilation: '-MQ")
  message(FATAL_ERROR
    "joined depfile flags leaked into manual module precompile.\n"
    "Output:\n${_joined_dep_out}\nErrors:\n${_joined_dep_err}")
endif()

set(_shell_tail_dir "${_work_dir}/shell_tail")
file(MAKE_DIRECTORY "${_shell_tail_dir}")
_gentest_write_file("${_shell_tail_dir}/provider.cppm" [[
export module gentest.retarget.shell_tail;
export int shell_tail_value() { return 17; }
]])
file(TO_CMAKE_PATH "${_shell_tail_dir}" _shell_tail_dir_norm)
file(TO_CMAKE_PATH "${_shell_tail_dir}/provider.cppm" _shell_tail_source_abs)
set(_shell_tail_command
  "${_clangxx_norm} -std=c++20 -c provider.cppm -o provider.o && ${CMAKE_COMMAND} -E cmake_transform_depfile "
  "Ninja gccdepfile ${_shell_tail_dir_norm} ${_shell_tail_dir_norm} ${_shell_tail_dir_norm} ${_shell_tail_dir_norm} deps.d deps.out")
string(JOIN "" _shell_tail_command ${_shell_tail_command})
_gentest_write_single_entry_compdb("${_shell_tail_dir}/compile_commands.json"
  DIRECTORY "${_shell_tail_dir_norm}"
  FILE "${_shell_tail_source_abs}"
  COMMAND "${_shell_tail_command}")

_gentest_run_codegen_expect_success(
  NAME "shell-tail command string"
  COMPDB_DIR "${_shell_tail_dir}"
  SOURCE_FILE "${_shell_tail_source_abs}")

set(_shell_tail_args_dir "${_work_dir}/shell_tail_arguments")
file(MAKE_DIRECTORY "${_shell_tail_args_dir}")
_gentest_write_file("${_shell_tail_args_dir}/provider.cppm" [[
export module gentest.retarget.shell_tail_arguments;
export int shell_tail_arguments_value() { return 19; }
]])
file(TO_CMAKE_PATH "${_shell_tail_args_dir}" _shell_tail_args_dir_norm)
file(TO_CMAKE_PATH "${_shell_tail_args_dir}/provider.cppm" _shell_tail_args_source_abs)
_gentest_write_single_entry_compdb("${_shell_tail_args_dir}/compile_commands.json"
  DIRECTORY "${_shell_tail_args_dir_norm}"
  FILE "${_shell_tail_args_source_abs}"
  ARGUMENTS
    "${_clangxx_norm}"
    "-std=c++20"
    "-c"
    "provider.cppm"
    "-o"
    "provider.o"
    "&&"
    "${CMAKE_COMMAND}"
    "-E"
    "cmake_transform_depfile"
    "Ninja"
    "gccdepfile"
    "${_shell_tail_args_dir_norm}"
    "${_shell_tail_args_dir_norm}"
    "${_shell_tail_args_dir_norm}"
    "${_shell_tail_args_dir_norm}"
    "deps.d"
    "deps.out")

_gentest_run_codegen_expect_success(
  NAME "shell-tail command arguments"
  COMPDB_DIR "${_shell_tail_args_dir}"
  SOURCE_FILE "${_shell_tail_args_source_abs}")

set(_extra_arg_shell_tail_dir "${_work_dir}/extra_arg_shell_tail")
file(MAKE_DIRECTORY "${_extra_arg_shell_tail_dir}")
_gentest_write_file("${_extra_arg_shell_tail_dir}/provider.cppm" [[
export module gentest.retarget.extra_arg_provider;
export int extra_arg_provider_value() { return 23; }
]])
_gentest_write_file("${_extra_arg_shell_tail_dir}/consumer.cppm" [[
export module gentest.retarget.extra_arg_consumer;
import gentest.retarget.extra_arg_provider;
export int extra_arg_consumer_value() { return extra_arg_provider_value(); }
]])
file(TO_CMAKE_PATH "${_extra_arg_shell_tail_dir}" _extra_arg_shell_tail_dir_norm)
file(TO_CMAKE_PATH "${_extra_arg_shell_tail_dir}/provider.cppm" _extra_arg_provider_source_abs)
file(TO_CMAKE_PATH "${_extra_arg_shell_tail_dir}/consumer.cppm" _extra_arg_consumer_source_abs)
set(_extra_arg_compdb "[\n")
_gentest_json_array(_provider_args_json "${_clangxx_norm}" "-std=c++20" "-c" "provider.cppm" "-o" "provider.o")
_gentest_json_array(_consumer_args_json "${_clangxx_norm}" "-std=c++20" "-c" "consumer.cppm" "-o" "consumer.o")
string(APPEND _extra_arg_compdb
  "  {\n"
  "    \"directory\": \"${_extra_arg_shell_tail_dir_norm}\",\n"
  "    \"file\": \"provider.cppm\",\n"
  "    \"arguments\": ${_provider_args_json}\n"
  "  },\n"
  "  {\n"
  "    \"directory\": \"${_extra_arg_shell_tail_dir_norm}\",\n"
  "    \"file\": \"consumer.cppm\",\n"
  "    \"arguments\": ${_consumer_args_json}\n"
  "  }\n"
  "]\n")
_gentest_write_file("${_extra_arg_shell_tail_dir}/compile_commands.json" "${_extra_arg_compdb}")

execute_process(
  COMMAND "${PROG}" --check --compdb "${_extra_arg_shell_tail_dir}"
    "${_extra_arg_provider_source_abs}" "${_extra_arg_consumer_source_abs}"
    --
    -isystem
    "${_extra_arg_shell_tail_dir_norm} && ${CMAKE_COMMAND} -E cmake_transform_depfile Ninja gccdepfile ${_extra_arg_shell_tail_dir_norm} ${_extra_arg_shell_tail_dir_norm} ${_extra_arg_shell_tail_dir_norm} ${_extra_arg_shell_tail_dir_norm} deps.d deps.out"
  RESULT_VARIABLE _extra_arg_shell_tail_rc
  OUTPUT_VARIABLE _extra_arg_shell_tail_out
  ERROR_VARIABLE _extra_arg_shell_tail_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _extra_arg_shell_tail_rc EQUAL 0)
  message(FATAL_ERROR
    "shell-tail extra clang arguments: gentest_codegen failed unexpectedly.\n"
    "Output:\n${_extra_arg_shell_tail_out}\nErrors:\n${_extra_arg_shell_tail_err}")
endif()

message(STATUS "Module command retargeting regression passed")
