if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_mode "aggregation")
if(DEFINED MODE AND NOT "${MODE}" STREQUAL "")
  set(_mode "${MODE}")
endif()

find_program(_real_clang NAMES clang++-22 clang++-21 clang++-20 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_manifest_depfile_aggregation")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_a_hpp "${_work_dir}/a.hpp")
set(_b_hpp "${_work_dir}/b.hpp")
set(_a_cpp "${_work_dir}/a.cpp")
set(_b_cpp "${_work_dir}/b.cpp")
set(_depfile "${_work_dir}/dep_manifest.d")
set(_output "${_work_dir}/dep_manifest.cpp")
set(_mock_registry "${_work_dir}/dep_manifest_mock_registry.hpp")
set(_mock_impl "${_work_dir}/dep_manifest_mock_impl.hpp")
set(_mock_registry_domain "${_work_dir}/dep_manifest_mock_registry__domain_0000_header.hpp")
set(_mock_impl_domain "${_work_dir}/dep_manifest_mock_impl__domain_0000_header.hpp")

file(COPY
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.cpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.cpp"
  DESTINATION "${_work_dir}")

file(TO_CMAKE_PATH "${_a_cpp}" _a_cpp_norm)
file(TO_CMAKE_PATH "${_b_cpp}" _b_cpp_norm)

set(_compile_command_args_a "${_real_clang_norm}")
set(_compile_command_args_b "${_real_clang_norm}")
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _compile_command_args_a "${TARGET_ARG}")
  list(APPEND _compile_command_args_b "${TARGET_ARG}")
endif()
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${_source_dir_norm}"
  APPLE_SYSROOT)
gentest_normalize_std_flag_for_compiler(_compdb_std "${_real_clang_norm}" "${CODEGEN_STD}")
gentest_normalize_include_args_for_compiler(_compdb_include_args "${_real_clang_norm}" ${_public_include_args})
list(APPEND _compile_command_args_a "${_compdb_std}" ${_compdb_include_args} "-I${_work_dir_norm}" "-c" "${_a_cpp_norm}")
list(APPEND _compile_command_args_a "-o" "${_work_dir_norm}/a.o")
list(APPEND _compile_command_args_b "${_compdb_std}" ${_compdb_include_args} "-I${_work_dir_norm}" "-c" "${_b_cpp_norm}")
list(APPEND _compile_command_args_b "-o" "${_work_dir_norm}/b.o")

gentest_fixture_make_compdb_entry(_a_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_a_cpp_norm}"
  ARGUMENTS ${_compile_command_args_a})
gentest_fixture_make_compdb_entry(_b_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_b_cpp_norm}"
  ARGUMENTS ${_compile_command_args_b})
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_a_entry}" "${_b_entry}")

function(_gentest_run_manifest_codegen depfile_path out_rc out_out out_err)
  execute_process(
    COMMAND
      "${PROG}"
      --output "${_output}"
      --mock-registry "${_mock_registry}"
      --mock-impl "${_mock_impl}"
      --depfile "${depfile_path}"
      --compdb "${_work_dir}"
      "${_a_cpp_norm}"
      "${_b_cpp_norm}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(${out_rc} "${_rc}" PARENT_SCOPE)
  set(${out_out} "${_out}" PARENT_SCOPE)
  set(${out_err} "${_err}" PARENT_SCOPE)
endfunction()

if(_mode STREQUAL "aggregation")
  _gentest_run_manifest_codegen("${_depfile}" _rc _out _err)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen failed while writing a manifest depfile. Output:\n${_out}\nErrors:\n${_err}")
  endif()

  file(READ "${_depfile}" _depfile_text)
  foreach(_target IN ITEMS
      "${_output}"
      "${_mock_registry}"
      "${_mock_impl}"
      "${_mock_registry_domain}"
      "${_mock_impl_domain}")
    get_filename_component(_target_name "${_target}" NAME)
    string(FIND "${_depfile_text}" "${_target_name}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "Manifest depfile is missing target '${_target_name}'. Full depfile:\n${_depfile_text}")
    endif()
  endforeach()
  foreach(_needle IN ITEMS "a.cpp" "a.hpp" "b.cpp" "b.hpp" "compile_commands.json")
    string(FIND "${_depfile_text}" "${_needle}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "Manifest depfile is missing '${_needle}'. Full depfile:\n${_depfile_text}")
    endif()
  endforeach()
elseif(_mode STREQUAL "write_failure")
  file(REMOVE_RECURSE "${_depfile}")
  file(MAKE_DIRECTORY "${_depfile}")

  _gentest_run_manifest_codegen("${_depfile}" _rc _out _err)

  set(_all "${_out}\n${_err}")
  if(NOT _rc EQUAL 1)
    message(FATAL_ERROR
      "Expected depfile write failure to exit with code 1, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  set(_expected "gentest_codegen: failed to write depfile '${_depfile}':")
  string(FIND "${_all}" "${_expected}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected depfile write failure output to contain '${_expected}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  foreach(_generated IN ITEMS
      "${_output}"
      "${_mock_registry}"
      "${_mock_impl}"
      "${_mock_registry_domain}"
      "${_mock_impl_domain}")
    if(NOT EXISTS "${_generated}")
      message(FATAL_ERROR
        "Expected generated output '${_generated}' to exist before depfile write failure. Output:\n${_all}")
    endif()
  endforeach()
elseif(_mode STREQUAL "escaped_paths")
  set(_special_dir "${_work_dir}/fixture dir#hash")
  set(_special_generated_dir "${_work_dir}/generated dir#hash")
  file(REMOVE_RECURSE "${_special_dir}" "${_special_generated_dir}")
  file(MAKE_DIRECTORY "${_special_dir}" "${_special_generated_dir}")

  file(COPY
    "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.hpp"
    "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.hpp"
    "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.cpp"
    "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.cpp"
    DESTINATION "${_special_dir}")

  file(TO_CMAKE_PATH "${_special_dir}" _special_dir_norm)
  file(TO_CMAKE_PATH "${_special_generated_dir}" _special_generated_dir_norm)

  set(_special_a_cpp "${_special_dir}/a.cpp")
  set(_special_b_cpp "${_special_dir}/b.cpp")
  # Keep the integration fixture portable by avoiding ':' on Windows while
  # still exercising Make-style colon escaping on POSIX hosts.
  if(WIN32)
    set(_special_stem "dep manifest#special$")
    set(_special_escaped_stem "dep\\ manifest\\#special\\$")
  else()
    set(_special_stem "dep manifest:special#$")
    set(_special_escaped_stem "dep\\ manifest\\:special\\#\\$")
  endif()
  set(_special_depfile "${_special_generated_dir}/${_special_stem}.d")
  set(_special_output "${_special_generated_dir}/${_special_stem}.cpp")
  set(_special_mock_registry "${_special_generated_dir}/${_special_stem}_mock_registry.hpp")
  set(_special_mock_impl "${_special_generated_dir}/${_special_stem}_mock_impl.hpp")
  set(_special_mock_registry_domain "${_special_generated_dir}/${_special_stem}_mock_registry__domain_0000_header.hpp")
  set(_special_mock_impl_domain "${_special_generated_dir}/${_special_stem}_mock_impl__domain_0000_header.hpp")

  file(TO_CMAKE_PATH "${_special_a_cpp}" _special_a_cpp_norm)
  file(TO_CMAKE_PATH "${_special_b_cpp}" _special_b_cpp_norm)

  set(_special_compile_args_a "${_real_clang_norm}")
  set(_special_compile_args_b "${_real_clang_norm}")
  if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
    list(APPEND _special_compile_args_a "${TARGET_ARG}")
    list(APPEND _special_compile_args_b "${TARGET_ARG}")
  endif()
  list(APPEND _special_compile_args_a "${_compdb_std}" ${_compdb_include_args} "-I${_special_dir_norm}" "-c" "${_special_a_cpp_norm}")
  list(APPEND _special_compile_args_a "-o" "${_special_dir_norm}/a.o")
  list(APPEND _special_compile_args_b "${_compdb_std}" ${_compdb_include_args} "-I${_special_dir_norm}" "-c" "${_special_b_cpp_norm}")
  list(APPEND _special_compile_args_b "-o" "${_special_dir_norm}/b.o")

  gentest_fixture_make_compdb_entry(_special_a_entry
    DIRECTORY "${_special_dir_norm}"
    FILE "${_special_a_cpp_norm}"
    ARGUMENTS ${_special_compile_args_a})
  gentest_fixture_make_compdb_entry(_special_b_entry
    DIRECTORY "${_special_dir_norm}"
    FILE "${_special_b_cpp_norm}"
    ARGUMENTS ${_special_compile_args_b})
  gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_special_a_entry}" "${_special_b_entry}")

  execute_process(
    COMMAND
      "${PROG}"
      --output "${_special_output}"
      --mock-registry "${_special_mock_registry}"
      --mock-impl "${_special_mock_impl}"
      --depfile "${_special_depfile}"
      --compdb "${_work_dir}"
      "${_special_a_cpp_norm}"
      "${_special_b_cpp_norm}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen failed while writing a depfile for escaped paths.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  file(READ "${_special_depfile}" _depfile_text)
  foreach(_needle IN ITEMS
      "generated\\ dir\\#hash/${_special_escaped_stem}.cpp"
      "generated\\ dir\\#hash/${_special_escaped_stem}_mock_registry.hpp"
      "generated\\ dir\\#hash/${_special_escaped_stem}_mock_impl.hpp"
      "generated\\ dir\\#hash/${_special_escaped_stem}_mock_registry__domain_0000_header.hpp"
      "generated\\ dir\\#hash/${_special_escaped_stem}_mock_impl__domain_0000_header.hpp"
      "fixture\\ dir\\#hash/a.cpp"
      "fixture\\ dir\\#hash/a.hpp"
      "fixture\\ dir\\#hash/b.cpp"
      "fixture\\ dir\\#hash/b.hpp"
      "compile_commands.json")
    string(FIND "${_depfile_text}" "${_needle}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "Escaped-path depfile is missing '${_needle}'. Full depfile:\n${_depfile_text}")
    endif()
  endforeach()
else()
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: unknown MODE='${_mode}'")
endif()
