if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenTuDepfileAggregation.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenTuDepfileAggregation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenTuDepfileAggregation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenTuDepfileAggregation.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_mode "aggregation")
if(DEFINED MODE AND NOT "${MODE}" STREQUAL "")
  set(_mode "${MODE}")
endif()

find_program(_real_clang NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_tu_depfile_aggregation")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_a_hpp "${_work_dir}/a.hpp")
set(_b_hpp "${_work_dir}/b.hpp")
set(_a_cpp "${_work_dir}/a.cpp")
set(_b_cpp "${_work_dir}/b.cpp")
set(_depfile "${_work_dir}/dep_tu.d")
set(_tu_output_dir "${_work_dir}/generated")
set(_output_a "${_tu_output_dir}/tu_a.gentest.h")
set(_output_b "${_tu_output_dir}/tu_b.gentest.h")
set(_mock_registry "${_work_dir}/dep_tu_mock_registry.hpp")
set(_mock_impl "${_work_dir}/dep_tu_mock_impl.hpp")
set(_mock_registry_domain "${_work_dir}/dep_tu_mock_registry__domain_0000_header.hpp")
set(_mock_impl_domain "${_work_dir}/dep_tu_mock_impl__domain_0000_header.hpp")
file(MAKE_DIRECTORY "${_tu_output_dir}")

file(COPY
  "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/a.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/b.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/a.cpp"
  "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/b.cpp"
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

function(_gentest_run_tu_codegen depfile_path out_rc out_out out_err)
  set(_extra_codegen_args)
  if(DEFINED _gentest_run_jobs AND NOT "${_gentest_run_jobs}" STREQUAL "")
    list(APPEND _extra_codegen_args "--jobs=${_gentest_run_jobs}")
  endif()
  if(DEFINED _gentest_run_timing_json AND NOT "${_gentest_run_timing_json}" STREQUAL "")
    list(APPEND _extra_codegen_args --timing-json "${_gentest_run_timing_json}")
  endif()
  if(DEFINED _gentest_run_artifact_manifest AND NOT "${_gentest_run_artifact_manifest}" STREQUAL "")
    list(APPEND _extra_codegen_args --artifact-manifest "${_gentest_run_artifact_manifest}")
  endif()
  set(_timeout_args)
  if(DEFINED _gentest_run_timeout AND NOT "${_gentest_run_timeout}" STREQUAL "")
    list(APPEND _timeout_args TIMEOUT "${_gentest_run_timeout}")
  endif()
  execute_process(
    COMMAND
      "${PROG}"
      ${_extra_codegen_args}
      --tu-out-dir "${_tu_output_dir}"
      --tu-header-output "${_output_a}"
      --tu-header-output "${_output_b}"
      --mock-registry "${_mock_registry}"
      --mock-impl "${_mock_impl}"
      --mock-domain-registry-output "${_mock_registry_domain}"
      --mock-domain-impl-output "${_mock_impl_domain}"
      --depfile "${depfile_path}"
      --compdb "${_work_dir}"
      "${_a_cpp_norm}"
      "${_b_cpp_norm}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    ${_timeout_args})

  set(${out_rc} "${_rc}" PARENT_SCOPE)
  set(${out_out} "${_out}" PARENT_SCOPE)
  set(${out_err} "${_err}" PARENT_SCOPE)
endfunction()

if(_mode STREQUAL "aggregation")
  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen failed while writing a TU-mode depfile. Output:\n${_out}\nErrors:\n${_err}")
  endif()

  file(READ "${_depfile}" _depfile_text)
  foreach(_target IN ITEMS
      "${_output_a}"
      "${_output_b}"
      "${_mock_registry}"
      "${_mock_impl}"
      "${_mock_registry_domain}"
      "${_mock_impl_domain}")
    get_filename_component(_target_name "${_target}" NAME)
    string(FIND "${_depfile_text}" "${_target_name}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "TU-mode depfile is missing target '${_target_name}'. Full depfile:\n${_depfile_text}")
    endif()
  endforeach()
  foreach(_needle IN ITEMS "a.cpp" "a.hpp" "b.cpp" "b.hpp" "compile_commands.json")
    string(FIND "${_depfile_text}" "${_needle}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "TU-mode depfile is missing '${_needle}'. Full depfile:\n${_depfile_text}")
    endif()
  endforeach()
elseif(_mode STREQUAL "write_failure")
  file(REMOVE_RECURSE "${_depfile}")
  file(MAKE_DIRECTORY "${_depfile}")

  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)

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
      "${_output_a}"
      "${_output_b}"
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
    "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/a.hpp"
    "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/b.hpp"
    "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/a.cpp"
    "${SOURCE_DIR}/tests/cmake/codegen_tu_depfile_aggregation/b.cpp"
    DESTINATION "${_special_dir}")

  file(TO_CMAKE_PATH "${_special_dir}" _special_dir_norm)
  file(TO_CMAKE_PATH "${_special_generated_dir}" _special_generated_dir_norm)

  set(_special_a_cpp "${_special_dir}/a.cpp")
  set(_special_b_cpp "${_special_dir}/b.cpp")
  # Keep the integration fixture portable by avoiding ':' on Windows while
  # still exercising Make-style colon escaping on POSIX hosts.
  if(WIN32)
    set(_special_stem "dep tu#special$")
    set(_special_escaped_stem "dep\\ tu\\#special\\$")
  else()
    set(_special_stem "dep tu:special#$")
    set(_special_escaped_stem "dep\\ tu\\:special\\#\\$")
  endif()
  set(_special_depfile "${_special_generated_dir}/${_special_stem}.d")
  set(_special_output_a "${_special_generated_dir}/${_special_stem}_a.gentest.h")
  set(_special_output_b "${_special_generated_dir}/${_special_stem}_b.gentest.h")
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
      --tu-out-dir "${_special_generated_dir}"
      --tu-header-output "${_special_output_a}"
      --tu-header-output "${_special_output_b}"
      --mock-registry "${_special_mock_registry}"
      --mock-impl "${_special_mock_impl}"
      --mock-domain-registry-output "${_special_mock_registry_domain}"
      --mock-domain-impl-output "${_special_mock_impl_domain}"
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
      "generated\\ dir\\#hash/${_special_escaped_stem}_a.gentest.h"
      "generated\\ dir\\#hash/${_special_escaped_stem}_b.gentest.h"
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
elseif(_mode STREQUAL "timing_json")
  set(_timing_json "${_work_dir}/timing.json")
  set(_artifact_manifest "${_work_dir}/dep_tu.artifact_manifest.json")
  set(_gentest_run_jobs 1)
  set(_gentest_run_timing_json "${_timing_json}")
  set(_gentest_run_artifact_manifest "${_artifact_manifest}")
  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen failed while writing timing JSON. Output:\n${_out}\nErrors:\n${_err}")
  endif()
  if(NOT EXISTS "${_timing_json}")
    message(FATAL_ERROR "gentest_codegen did not write requested timing JSON '${_timing_json}'")
  endif()

  file(READ "${_timing_json}" _timing_text)
  string(JSON _timing_schema GET "${_timing_text}" schema)
  if(NOT _timing_schema STREQUAL "gentest.codegen.timing.v1")
    message(FATAL_ERROR "Unexpected timing schema '${_timing_schema}':\n${_timing_text}")
  endif()
  string(JSON _timing_unit GET "${_timing_text}" duration_unit)
  if(NOT _timing_unit STREQUAL "microseconds")
    message(FATAL_ERROR "Unexpected timing duration unit '${_timing_unit}':\n${_timing_text}")
  endif()
  string(JSON _phase_count LENGTH "${_timing_text}" phases)
  set(_required_phase_names startup compdb scan-deps pcm merge emit mock depfile)
  set(_found_required_phase_names)
  set(_parse_count 0)
  foreach(_phase_index RANGE 0 ${_phase_count})
    if(_phase_index EQUAL _phase_count)
      break()
    endif()
    string(JSON _phase_name GET "${_timing_text}" phases ${_phase_index} name)
    string(JSON _duration_type TYPE "${_timing_text}" phases ${_phase_index} duration_us)
    if(NOT _duration_type STREQUAL "NUMBER")
      message(FATAL_ERROR "Timing phase '${_phase_name}' has non-numeric duration_us:\n${_timing_text}")
    endif()
    if(_phase_name STREQUAL "parse")
      math(EXPR _parse_count "${_parse_count} + 1")
      string(JSON _source_type TYPE "${_timing_text}" phases ${_phase_index} source)
      string(JSON _tu_index_type TYPE "${_timing_text}" phases ${_phase_index} tu_index)
      if(NOT _source_type STREQUAL "STRING" OR NOT _tu_index_type STREQUAL "NUMBER")
        message(FATAL_ERROR "Per-TU parse timing record is missing source or tu_index:\n${_timing_text}")
      endif()
    endif()
    list(FIND _required_phase_names "${_phase_name}" _required_phase_index)
    if(NOT _required_phase_index EQUAL -1)
      list(APPEND _found_required_phase_names "${_phase_name}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _found_required_phase_names)
  foreach(_required_phase_name IN LISTS _required_phase_names)
    list(FIND _found_required_phase_names "${_required_phase_name}" _found_phase_index)
    if(_found_phase_index EQUAL -1)
      message(FATAL_ERROR "Timing JSON is missing '${_required_phase_name}' phase:\n${_timing_text}")
    endif()
  endforeach()
  if(NOT _parse_count EQUAL 2)
    message(FATAL_ERROR "Expected one timing parse record per input TU (2), got ${_parse_count}:\n${_timing_text}")
  endif()

  set(_serial_dir "${_work_dir}/serial")
  file(MAKE_DIRECTORY "${_serial_dir}")
  set(_generated_artifacts
    "${_output_a}"
    "${_output_b}"
    "${_mock_registry}"
    "${_mock_impl}"
    "${_mock_registry_domain}"
    "${_mock_impl_domain}"
    "${_artifact_manifest}"
    "${_depfile}")
  foreach(_artifact IN LISTS _generated_artifacts)
    get_filename_component(_artifact_name "${_artifact}" NAME)
    file(COPY_FILE "${_artifact}" "${_serial_dir}/${_artifact_name}" ONLY_IF_DIFFERENT)
  endforeach()

  set(_gentest_run_jobs 2)
  set(_gentest_run_timing_json "${_work_dir}/timing_parallel.json")
  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "parallel gentest_codegen failed while comparing generated artifacts. Output:\n${_out}\nErrors:\n${_err}")
  endif()
  foreach(_artifact IN LISTS _generated_artifacts)
    get_filename_component(_artifact_name "${_artifact}" NAME)
    file(SHA256 "${_artifact}" _parallel_hash)
    file(SHA256 "${_serial_dir}/${_artifact_name}" _serial_hash)
    if(NOT _parallel_hash STREQUAL _serial_hash)
      message(FATAL_ERROR "Serial and parallel generated artifact differ: '${_artifact_name}'")
    endif()
  endforeach()
elseif(_mode STREQUAL "timing_write_failure")
  set(_gentest_run_timing_json "${_work_dir}/timing-as-directory.json")
  file(MAKE_DIRECTORY "${_gentest_run_timing_json}")
  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)
  if(NOT _rc EQUAL 1)
    message(FATAL_ERROR
      "Expected timing JSON publication failure to exit with code 1, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  set(_all "${_out}\n${_err}")
  string(FIND "${_all}" "failed to publish timing JSON" _timing_failure_pos)
  if(_timing_failure_pos EQUAL -1)
    message(FATAL_ERROR "Expected timing JSON publication failure diagnostic. Output:\n${_all}")
  endif()
elseif(_mode STREQUAL "timing_dependency_collision")
  foreach(_dependency IN ITEMS "${_a_hpp}" "${_b_hpp}")
    file(SHA256 "${_dependency}" _dependency_hash_before)
    set(_gentest_run_timing_json "${_dependency}")
    _gentest_run_tu_codegen("${_depfile}" _rc _out _err)
    if(NOT _rc EQUAL 1)
      message(FATAL_ERROR
        "Expected timing JSON dependency collision for '${_dependency}' to exit with code 1, got ${_rc}.\n"
        "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif()
    set(_all "${_out}\n${_err}")
    string(FIND "${_all}" "conflicts with discovered dependency" _dependency_collision_pos)
    if(_dependency_collision_pos EQUAL -1)
      message(FATAL_ERROR "Expected timing JSON dependency collision diagnostic for '${_dependency}'. Output:\n${_all}")
    endif()
    file(SHA256 "${_dependency}" _dependency_hash_after)
    if(NOT "${_dependency_hash_after}" STREQUAL "${_dependency_hash_before}")
      message(FATAL_ERROR "Timing JSON collision modified discovered dependency '${_dependency}'")
    endif()
    foreach(_generated IN ITEMS
        "${_output_a}"
        "${_output_b}"
        "${_mock_registry}"
        "${_mock_impl}"
        "${_mock_registry_domain}"
        "${_mock_impl_domain}"
        "${_depfile}")
      if(EXISTS "${_generated}")
        message(FATAL_ERROR "Timing JSON dependency collision created '${_generated}' before failing")
      endif()
    endforeach()
  endforeach()
elseif(_mode STREQUAL "timing_nonregular_destination")
  if(WIN32)
    message(STATUS "GENTEST_SKIP_TEST: FIFO timing destination regression is POSIX-only")
    return()
  endif()
  find_program(_mkfifo NAMES mkfifo)
  if(NOT _mkfifo)
    message(STATUS "GENTEST_SKIP_TEST: mkfifo is unavailable")
    return()
  endif()
  set(_gentest_run_timing_json "${_work_dir}/timing.fifo")
  execute_process(COMMAND "${_mkfifo}" "${_gentest_run_timing_json}" RESULT_VARIABLE _mkfifo_rc ERROR_VARIABLE _mkfifo_err)
  if(NOT _mkfifo_rc EQUAL 0)
    message(STATUS "GENTEST_SKIP_TEST: could not create FIFO timing destination: ${_mkfifo_err}")
    return()
  endif()
  # A regression that writes directly to the FIFO would block.  The timeout
  # turns that into a deterministic test failure rather than a hung CTest.
  set(_gentest_run_timeout 15)
  _gentest_run_tu_codegen("${_depfile}" _rc _out _err)
  unset(_gentest_run_timeout)
  if(NOT _rc EQUAL 1)
    message(FATAL_ERROR
      "Expected FIFO timing JSON publication failure to exit with code 1, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  set(_all "${_out}\n${_err}")
  string(FIND "${_all}" "failed to publish timing JSON" _timing_failure_pos)
  string(FIND "${_all}" "not a regular file" _nonregular_pos)
  if(_timing_failure_pos EQUAL -1 OR _nonregular_pos EQUAL -1)
    message(FATAL_ERROR "Expected FIFO timing JSON non-regular-file diagnostic. Output:\n${_all}")
  endif()

  set(_timing_link_target "${_work_dir}/timing-link-target")
  set(_gentest_run_timing_json "${_work_dir}/timing-link.json")
  file(MAKE_DIRECTORY "${_timing_link_target}")
  file(CREATE_LINK "${_timing_link_target}" "${_gentest_run_timing_json}" SYMBOLIC RESULT _timing_link_error)
  if(NOT "${_timing_link_error}" STREQUAL "0")
    message(STATUS "GENTEST_SKIP_TEST: could not create symlink-to-directory timing destination: ${_timing_link_error}")
  else()
    set(_gentest_run_timeout 15)
    _gentest_run_tu_codegen("${_depfile}" _link_rc _link_out _link_err)
    unset(_gentest_run_timeout)
    if(NOT _link_rc EQUAL 1)
      message(FATAL_ERROR
        "Expected symlink-to-directory timing JSON publication failure to exit with code 1, got ${_link_rc}.\n"
        "--- stdout ---\n${_link_out}\n--- stderr ---\n${_link_err}")
    endif()
    set(_link_all "${_link_out}\n${_link_err}")
    string(FIND "${_link_all}" "failed to publish timing JSON" _link_timing_failure_pos)
    string(FIND "${_link_all}" "not a regular file" _link_nonregular_pos)
    if(_link_timing_failure_pos EQUAL -1 OR _link_nonregular_pos EQUAL -1)
      message(FATAL_ERROR "Expected symlink-to-directory timing JSON non-regular-file diagnostic. Output:\n${_link_all}")
    endif()
  endif()
else()
  message(FATAL_ERROR "CheckCodegenTuDepfileAggregation.cmake: unknown MODE='${_mode}'")
endif()
