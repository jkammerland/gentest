# Requires the standard _gentest_add_cmake_helper_test variables.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_ROOT OR NOT DEFINED GENTEST_SOURCE_DIR OR NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckHeaderDeclarationRegistration.cmake: required helper variable missing")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("header-declaration registration regression: fixture uses a single-config Ninja build")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/header_declaration_registration")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("header-declaration registration regression: clang/clang++ not found")
  return()
endif()
gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("header-declaration registration regression: ${_supported_ninja_reason}")
  return()
endif()

set(_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

function(_gentest_registration_digest build_dir out_var)
  file(GLOB _registration_files "${build_dir}/generated/*.header_registration.gentest.cpp")
  list(SORT _registration_files)
  set(_contents "")
  foreach(_registration_file IN LISTS _registration_files)
    file(READ "${_registration_file}" _registration_text)
    string(APPEND _contents "${_registration_file}\n${_registration_text}\n")
  endforeach()
  file(GLOB _manifest_files "${build_dir}/generated/header_declaration_registration_tests*.artifact_manifest.json")
  list(LENGTH _manifest_files _manifest_count)
  if(NOT _manifest_count EQUAL 1)
    message(FATAL_ERROR "Expected one additive artifact manifest, got ${_manifest_count}")
  endif()
  list(GET _manifest_files 0 _manifest_file)
  file(READ "${_manifest_file}" _manifest_text)
  string(APPEND _contents "${_manifest_text}\n")
  string(SHA256 _digest "${_contents}")
  set(${out_var} "${_digest}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_fixture_build_failure scenario expected_text)
  set(_scenario_dir "${_work_dir}/${scenario}")
  set(_scenario_src "${_scenario_dir}/src")
  set(_scenario_build "${_scenario_dir}/build")
  file(REMOVE_RECURSE "${_scenario_dir}")
  file(MAKE_DIRECTORY "${_scenario_dir}")
  file(COPY "${SOURCE_DIR}/" DESTINATION "${_scenario_src}")

  # Callers prepare mutations in SOURCE_DIR-shaped variables through the
  # scenario-specific files staged before invoking this helper.
  if(DEFINED GENTEST_SCENARIO_FILE AND EXISTS "${GENTEST_SCENARIO_FILE}")
    file(COPY "${GENTEST_SCENARIO_FILE}" DESTINATION "${_scenario_src}")
  endif()
  if(DEFINED GENTEST_SCENARIO_CASES_A_PREFIX AND NOT "${GENTEST_SCENARIO_CASES_A_PREFIX}" STREQUAL "")
    file(READ "${_scenario_src}/cases_a.cpp" _cases_a)
    file(WRITE "${_scenario_src}/cases_a.cpp" "${GENTEST_SCENARIO_CASES_A_PREFIX}\n${_cases_a}")
  endif()
  if(DEFINED GENTEST_SCENARIO_CASES_B_PREFIX AND NOT "${GENTEST_SCENARIO_CASES_B_PREFIX}" STREQUAL "")
    file(READ "${_scenario_src}/cases_b.cpp" _cases_b)
    file(WRITE "${_scenario_src}/cases_b.cpp" "${GENTEST_SCENARIO_CASES_B_PREFIX}\n${_cases_b}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_scenario_src}" -B "${_scenario_build}" ${_cache_args}
    WORKING_DIRECTORY "${_scenario_dir}"
    RESULT_VARIABLE _configure_rc
    OUTPUT_VARIABLE _configure_stdout
    ERROR_VARIABLE _configure_stderr)
  if(NOT _configure_rc EQUAL 0)
    message(FATAL_ERROR "Scenario '${scenario}' failed to configure.\n${_configure_stdout}\n${_configure_stderr}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_scenario_build}" --target header_declaration_registration_tests
    WORKING_DIRECTORY "${_scenario_dir}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_stdout
    ERROR_VARIABLE _build_stderr)
  if(_build_rc EQUAL 0)
    message(FATAL_ERROR "Scenario '${scenario}' unexpectedly built successfully")
  endif()
  string(FIND "${_build_stdout}${_build_stderr}" "${expected_text}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "Scenario '${scenario}' missed expected diagnostic '${expected_text}'.\n${_build_stdout}\n${_build_stderr}")
  endif()
endfunction()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_build_dir}" ${_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
if(NOT CMAKE_HOST_WIN32 AND NOT IS_SYMLINK "${_src_dir}/header_a_symlink.hpp")
  message(FATAL_ERROR "The supported-host alias regression must exercise a real symbolic link")
endif()
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target
    header_declaration_registration_tests
    header_declaration_wrapper_compat_tests
    header_declaration_dual_1
    header_declaration_dual_2
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/header_declaration_registration_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()
gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_output)
gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-json
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _json_inventory)
foreach(_metadata IN ITEMS
    "\"requirements\":[\"REQ-107\"]"
    "\"owner\":\"codegen\""
    "\"async\":true"
    "\"kind\":\"bench\""
    "\"itemsPerCall\":2"
    "\"kind\":\"jitter\""
    "\"tags\":[\"death\"]")
  string(FIND "${_json_inventory}" "${_metadata}" _metadata_pos)
  if(_metadata_pos EQUAL -1)
    message(FATAL_ERROR "Header-declaration JSON inventory missed metadata '${_metadata}'.\n${_json_inventory}")
  endif()
endforeach()
foreach(_name IN ITEMS
    "header_declaration/a"
    "header_declaration/b"
    "header_declaration/context_a"
    "header_declaration/context_b"
    "header_declaration/fallback"
    "header_declaration/fixture_parity"
    "header_declaration/inline"
    "header_declaration/macro_pair_one"
    "header_declaration/macro_pair_two"
    "header_declaration/explicit_mock"
    "header_declaration/redeclared"
    "header_declaration/same_basename_a"
    "header_declaration/same_basename_b"
    "header_declaration/spaced_path"
    "macro_case_one"
    "macro_case_two")
  string(FIND "${_list_output}" "${_name}" _name_pos)
  if(_name_pos EQUAL -1)
    message(FATAL_ERROR "Expected '${_name}' in --list-tests output.\n${_list_output}")
  endif()
  gentest_check_run_or_fail(COMMAND "${_exe}" "--run=${_name}" WORKING_DIRECTORY "${_build_dir}" STRIP_TRAILING_WHITESPACE)
endforeach()
gentest_check_run_or_fail(
  COMMAND "${_exe}" "--filter=*fixture_parity*" "--repeat=2" --shuffle --seed 107
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_compat_exe "${_build_dir}/header_declaration_wrapper_compat_tests")
if(CMAKE_HOST_WIN32)
  string(APPEND _compat_exe ".exe")
endif()
gentest_check_run_or_fail(
  COMMAND "${_compat_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _compat_list_output)
if(NOT _compat_list_output STREQUAL _list_output)
  message(FATAL_ERROR
    "Additive and wrapper-compatibility --list inventories differ.\nAdditive:\n${_list_output}\nCompatibility:\n${_compat_list_output}")
endif()

# The scanner is always the host Clang tool, but the authored and additive
# registration sources must remain valid when the target compiler is GCC.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  find_program(_gcc NAMES gcc)
  find_program(_gxx NAMES g++)
  if(_gcc AND _gxx)
    set(_gcc_build_dir "${_work_dir}/gcc-build")
    set(_gcc_cache_args ${_cache_args})
    list(FILTER _gcc_cache_args EXCLUDE REGEX "^-DCMAKE_(C|CXX)_COMPILER=")
    list(APPEND _gcc_cache_args "-DCMAKE_C_COMPILER=${_gcc}" "-DCMAKE_CXX_COMPILER=${_gxx}")
    gentest_check_run_or_fail(
      COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_gcc_build_dir}" ${_gcc_cache_args}
      WORKING_DIRECTORY "${_work_dir}"
      STRIP_TRAILING_WHITESPACE)
    gentest_check_run_or_fail(
      COMMAND "${CMAKE_COMMAND}" --build "${_gcc_build_dir}" --target header_declaration_registration_tests
      WORKING_DIRECTORY "${_work_dir}"
      STRIP_TRAILING_WHITESPACE)
    file(READ "${_gcc_build_dir}/compile_commands.json" _gcc_compdb)
    string(JSON _gcc_compdb_count LENGTH "${_gcc_compdb}")
    math(EXPR _gcc_compdb_last "${_gcc_compdb_count} - 1")
    foreach(_gcc_compdb_idx RANGE 0 ${_gcc_compdb_last})
      string(JSON _gcc_compdb_file GET "${_gcc_compdb}" ${_gcc_compdb_idx} file)
      if(_gcc_compdb_file MATCHES "[/\\\\](cases_a|cases_b|cases_c|cases_rich|mock_cases|dual_target)\\.cpp$")
        string(JSON _gcc_compdb_command GET "${_gcc_compdb}" ${_gcc_compdb_idx} command)
        foreach(_unsupported_clangd_flag IN ITEMS "-fmodules-ts" "-fmodule-mapper=" "-fdeps-format=p1689r5")
          string(FIND "${_gcc_compdb_command}" "${_unsupported_clangd_flag}" _unsupported_clangd_flag_pos)
          if(NOT _unsupported_clangd_flag_pos EQUAL -1)
            message(FATAL_ERROR
              "Ordinary additive authored slot '${_gcc_compdb_file}' must disable CMake's GCC module scan so clangd can consume its exact command; found '${_unsupported_clangd_flag}'.\n${_gcc_compdb_command}")
          endif()
        endforeach()
      endif()
    endforeach()
    set(_gcc_exe "${_gcc_build_dir}/header_declaration_registration_tests")
    gentest_check_run_or_fail(
      COMMAND "${_gcc_exe}" --run=header_declaration/a
      WORKING_DIRECTORY "${_gcc_build_dir}"
      STRIP_TRAILING_WHITESPACE)
  endif()
else()
  # A VS developer environment exposes cl.exe. When present, prove the same
  # host-Clang code generator preserves the MSVC-final forced-include context.
  find_program(_cl NAMES cl.exe cl)
  if(_cl)
    set(_msvc_build_dir "${_work_dir}/msvc-build")
    set(_msvc_cache_args ${_cache_args})
    list(FILTER _msvc_cache_args EXCLUDE REGEX "^-DCMAKE_(C|CXX)_COMPILER=")
    list(APPEND _msvc_cache_args "-DCMAKE_C_COMPILER=${_cl}" "-DCMAKE_CXX_COMPILER=${_cl}")
    gentest_check_run_or_fail(
      COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_msvc_build_dir}" ${_msvc_cache_args}
      WORKING_DIRECTORY "${_work_dir}"
      STRIP_TRAILING_WHITESPACE)
    gentest_check_run_or_fail(
      COMMAND "${CMAKE_COMMAND}" --build "${_msvc_build_dir}" --target header_declaration_registration_tests
      WORKING_DIRECTORY "${_work_dir}"
      STRIP_TRAILING_WHITESPACE)
    gentest_check_run_or_fail(
      COMMAND "${_msvc_build_dir}/header_declaration_registration_tests.exe" --run=header_declaration/a
      WORKING_DIRECTORY "${_msvc_build_dir}"
      STRIP_TRAILING_WHITESPACE)
  endif()
endif()

foreach(_name IN ITEMS
    "rich/parameter(1)"
    "rich/parameter(2)"
    "rich/rows(1, 2)"
    "rich/rows(3, 4)"
    "rich/range(1)"
    "rich/range(2)"
    "rich/range(3)"
    "rich/template<int>"
    "rich/template<long>"
    "rich/async"
    "rich/bench"
    "rich/jitter")
  string(FIND "${_list_output}" "${_name}" _name_pos)
  if(_name_pos EQUAL -1)
    message(FATAL_ERROR "Expected rich header-declaration case '${_name}'.\n${_list_output}")
  endif()
endforeach()
gentest_check_run_or_fail(
  COMMAND "${_exe}" "--run=header_declaration/rich/async"
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_build_dir}" --show-only=json-v1
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _ctest_inventory)
foreach(_name IN ITEMS "rich/async" "rich/bench" "rich/jitter")
  string(FIND "${_ctest_inventory}" "${_name}" _ctest_name_pos)
  if(_ctest_name_pos EQUAL -1)
    message(FATAL_ERROR "CTest discovery missed '${_name}'.\n${_ctest_inventory}")
  endif()
endforeach()

string(JSON _ctest_count LENGTH "${_ctest_inventory}" tests)
set(_additive_ctest_cases "")
set(_compat_ctest_cases "")
if(_ctest_count GREATER 0)
  math(EXPR _ctest_last "${_ctest_count} - 1")
  foreach(_ctest_idx RANGE 0 ${_ctest_last})
    string(JSON _ctest_name GET "${_ctest_inventory}" tests ${_ctest_idx} name)
    if(_ctest_name MATCHES "^additive::")
      string(REGEX REPLACE "^additive::" "" _ctest_case "${_ctest_name}")
      list(APPEND _additive_ctest_cases "${_ctest_case}")
    elseif(_ctest_name MATCHES "^compat::")
      string(REGEX REPLACE "^compat::" "" _ctest_case "${_ctest_name}")
      list(APPEND _compat_ctest_cases "${_ctest_case}")
    endif()
  endforeach()
endif()
list(SORT _additive_ctest_cases)
list(SORT _compat_ctest_cases)
if(NOT _additive_ctest_cases STREQUAL _compat_ctest_cases)
  message(FATAL_ERROR
    "Additive and wrapper-compatibility CTest inventories differ.\nAdditive: ${_additive_ctest_cases}\nCompatibility: ${_compat_ctest_cases}")
endif()

foreach(_dual_variant IN ITEMS 1 2)
  set(_dual_exe "${_build_dir}/header_declaration_dual_${_dual_variant}")
  if(CMAKE_HOST_WIN32)
    string(APPEND _dual_exe ".exe")
  endif()
  gentest_check_run_or_fail(
    COMMAND "${_dual_exe}" --list-tests
    WORKING_DIRECTORY "${_build_dir}"
    STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE _dual_list)
  if(_dual_variant EQUAL 1)
    set(_dual_expected "header_declaration/dual_one")
    set(_dual_unexpected "header_declaration/dual_two")
  else()
    set(_dual_expected "header_declaration/dual_two")
    set(_dual_unexpected "header_declaration/dual_one")
  endif()
  string(FIND "${_dual_list}" "${_dual_expected}" _dual_expected_pos)
  string(FIND "${_dual_list}" "${_dual_unexpected}" _dual_unexpected_pos)
  if(_dual_expected_pos EQUAL -1 OR NOT _dual_unexpected_pos EQUAL -1)
    message(FATAL_ERROR "Target-local compile context was not preserved for dual target ${_dual_variant}.\n${_dual_list}")
  endif()
  gentest_check_run_or_fail(
    COMMAND "${_dual_exe}" "--run=${_dual_expected}"
    WORKING_DIRECTORY "${_build_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()

set(_registration_a "${_build_dir}/generated/tu_0000_cases_a.header_registration.gentest.cpp")
set(_registration_b "${_build_dir}/generated/tu_0001_cases_b.header_registration.gentest.cpp")
set(_registration_c "${_build_dir}/generated/tu_0005_contextual_cases.header_registration.gentest.cpp")
set(_registration_fallback "${_build_dir}/generated/tu_0006_fallback_cases.header_registration.gentest.cpp")
foreach(_file IN ITEMS "${_registration_a}" "${_registration_b}" "${_registration_c}" "${_registration_fallback}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Expected generated registration source '${_file}'")
  endif()
endforeach()
file(READ "${_registration_a}" _registration_a_text)
file(READ "${_registration_b}" _registration_b_text)
foreach(_header IN ITEMS "header_a.hpp" "redeclaration_a.hpp")
  string(FIND "${_registration_a_text}" "${_header}" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Assigned registration source is missing '${_header}'.\n${_registration_a_text}")
  endif()
endforeach()
string(FIND "${_registration_a_text}" "redeclaration_b.hpp" _wrong_redeclaration_header_pos)
if(NOT _wrong_redeclaration_header_pos EQUAL -1)
  message(FATAL_ERROR
    "Equivalent redeclarations must retain the selected owner's header context.\n${_registration_a_text}")
endif()
file(READ "${_registration_b}" _registration_b_text)
foreach(_header IN ITEMS "header_b.hpp")
  string(FIND "${_registration_b_text}" "${_header}" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Assigned registration source is missing '${_header}'.\n${_registration_b_text}")
  endif()
endforeach()
foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp")
  string(FIND "${_registration_a_text}" "${_source}" _source_pos)
  if(NOT _source_pos EQUAL -1)
    message(FATAL_ERROR "Generated registration source must not include authored source '${_source}'.\n${_registration_a_text}")
  endif()
endforeach()
file(READ "${_registration_c}" _registration_c_text)
string(FIND "${_registration_c_text}" "No gentest registrations were assigned" _empty_pos)
if(_empty_pos EQUAL -1)
  message(FATAL_ERROR "Unassigned slot must emit a valid empty registration source.\n${_registration_c_text}")
endif()
file(READ "${_registration_fallback}" _registration_fallback_text)
string(FIND "${_registration_fallback_text}" "fallback_cases.hpp" _fallback_header_pos)
if(_fallback_header_pos EQUAL -1)
  message(FATAL_ERROR "Unreachable target-listed header must use its fallback slot.\n${_registration_fallback_text}")
endif()

file(GLOB _all_registration_sources "${_build_dir}/generated/*.header_registration.gentest.cpp")
list(LENGTH _all_registration_sources _registration_count)
if(NOT _registration_count EQUAL 15)
  message(FATAL_ERROR "Expected one stable registration source for 15 predeclared slots, got ${_registration_count}")
endif()
foreach(_registration_source IN LISTS _all_registration_sources)
  file(READ "${_registration_source}" _registration_text)
  string(REGEX MATCH "#include [\"<][^\">]*\\.(cc|cpp|cxx)[\">]" _authored_include "${_registration_text}")
  if(_authored_include)
    message(FATAL_ERROR "Generated registration source includes an authored implementation: ${_registration_source}\n${_registration_text}")
  endif()
endforeach()

file(GLOB _artifact_manifests "${_build_dir}/generated/header_declaration_registration_tests*.artifact_manifest.json")
list(LENGTH _artifact_manifests _artifact_manifest_count)
if(NOT _artifact_manifest_count EQUAL 1)
  message(FATAL_ERROR "Expected one additive registration artifact manifest, got ${_artifact_manifest_count}")
endif()
list(GET _artifact_manifests 0 _artifact_manifest)
file(READ "${_artifact_manifest}" _artifact_manifest_text)
string(JSON _manifest_source_count LENGTH "${_artifact_manifest_text}" sources)
string(JSON _manifest_artifact_count LENGTH "${_artifact_manifest_text}" artifacts)
if(NOT _manifest_source_count EQUAL 15 OR NOT _manifest_artifact_count EQUAL 15)
  message(FATAL_ERROR
    "Expected one source/artifact manifest entry for every predeclared slot, got ${_manifest_source_count}/${_manifest_artifact_count}")
endif()
math(EXPR _manifest_last "${_manifest_source_count} - 1")
foreach(_manifest_idx RANGE 0 ${_manifest_last})
  string(JSON _source_kind GET "${_artifact_manifest_text}" sources ${_manifest_idx} kind)
  string(JSON _source_fingerprint GET "${_artifact_manifest_text}" sources ${_manifest_idx} compile_context_fingerprint)
  string(JSON _artifact_fingerprint GET "${_artifact_manifest_text}" artifacts ${_manifest_idx} compile_context_fingerprint)
  string(JSON _target_attachment GET "${_artifact_manifest_text}" artifacts ${_manifest_idx} target_attachment)
  string(JSON _includes_authored_source GET "${_artifact_manifest_text}" artifacts ${_manifest_idx} includes_authored_source)
  string(JSON _replaces_authored_source GET "${_artifact_manifest_text}" artifacts ${_manifest_idx} replaces_authored_source)
  string(LENGTH "${_source_fingerprint}" _fingerprint_length)
  if(NOT _source_kind STREQUAL "cxx-header-declaration-registration" OR
      NOT _fingerprint_length EQUAL 71 OR
      NOT _source_fingerprint STREQUAL _artifact_fingerprint OR
      NOT _target_attachment STREQUAL "append-generated-source" OR
      _includes_authored_source OR _replaces_authored_source)
    message(FATAL_ERROR "Invalid additive artifact manifest entry ${_manifest_idx}:\n${_artifact_manifest_text}")
  endif()
endforeach()

file(READ "${_build_dir}/compile_commands.json" _compdb)
file(TO_CMAKE_PATH "${_src_dir}" _compdb_src_dir)
string(JSON _compdb_count LENGTH "${_compdb}")
math(EXPR _compdb_last "${_compdb_count} - 1")
set(_compdb_authored_sources "")
set(_dual_compdb_count 0)
foreach(_compdb_idx RANGE 0 ${_compdb_last})
  string(JSON _compdb_file GET "${_compdb}" ${_compdb_idx} file)
  file(TO_CMAKE_PATH "${_compdb_file}" _compdb_file)
  foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp" "cases_c.cpp" "cases_rich.cpp" "mock_cases.cpp")
    if(_compdb_file STREQUAL "${_compdb_src_dir}/${_source}")
      list(APPEND _compdb_authored_sources "${_source}")
    endif()
  endforeach()
  if(_compdb_file STREQUAL "${_compdb_src_dir}/dual_target.cpp")
    math(EXPR _dual_compdb_count "${_dual_compdb_count} + 1")
  endif()
endforeach()
foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp" "cases_c.cpp" "cases_rich.cpp" "mock_cases.cpp")
  if(NOT _source IN_LIST _compdb_authored_sources)
    message(FATAL_ERROR "Authored source '${_source}' must retain its direct compilation-database entry.\n${_compdb}")
  endif()
endforeach()
if(NOT _dual_compdb_count EQUAL 3)
  message(FATAL_ERROR "A shared authored source must retain one compile command per consuming target.\n${_compdb}")
endif()

file(READ "${_build_dir}/target_sources.txt" _target_sources)
foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp" "cases_c.cpp" "cases_rich.cpp" "mock_cases.cpp")
  set(_source_occurrences ${_target_sources})
  list(FILTER _source_occurrences INCLUDE REGEX "(^|[/\\\\])${_source}$")
  list(LENGTH _source_occurrences _source_count)
  if(NOT _source_count EQUAL 1)
    message(FATAL_ERROR "Authored target source '${_source}' must remain attached exactly once.\n${_target_sources}")
  endif()
endforeach()

# Worker-count and cache-hit changes must not affect output ownership or bytes.
_gentest_registration_digest("${_build_dir}" _jobs1_digest)
foreach(_jobs IN ITEMS 2 0)
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" -S "${_src_dir}" -B "${_build_dir}" "-DGENTEST_CODEGEN_JOBS=${_jobs}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_declaration_registration_tests
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  _gentest_registration_digest("${_build_dir}" _jobs_digest)
  if(NOT _jobs_digest STREQUAL _jobs1_digest)
    message(FATAL_ERROR "Registration output bytes changed for GENTEST_CODEGEN_JOBS=${_jobs}")
  endif()
endforeach()

# Force a late completion for the normally first owner slot. Ownership and
# bytes must still come from the serial merge order, not worker completion.
file(REMOVE "${_registration_a}")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN_TEST_DELAY_SLOT=0:250"
    "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_declaration_registration_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
_gentest_registration_digest("${_build_dir}" _perturbed_digest)
if(NOT _perturbed_digest STREQUAL _jobs1_digest)
  message(FATAL_ERROR "Registration output bytes changed when worker completion order was perturbed")
endif()

file(TIMESTAMP "${_registration_a}" _registration_timestamp_before UTC)
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_declaration_registration_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _noop_rc
  OUTPUT_VARIABLE _noop_stdout
  ERROR_VARIABLE _noop_stderr)
if(NOT _noop_rc EQUAL 0)
  message(FATAL_ERROR "No-op build failed.\n${_noop_stdout}\n${_noop_stderr}")
endif()
file(TIMESTAMP "${_registration_a}" _registration_timestamp_after UTC)
if(NOT _registration_timestamp_after STREQUAL _registration_timestamp_before)
  message(FATAL_ERROR "No-op build rewrote generated registration sources")
endif()

file(APPEND "${_src_dir}/header_a.hpp" "\n// dependency invalidation probe\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_declaration_registration_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _header_rebuild_rc
  OUTPUT_VARIABLE _header_rebuild_stdout
  ERROR_VARIABLE _header_rebuild_stderr)
if(NOT _header_rebuild_rc EQUAL 0)
  message(FATAL_ERROR "Header dependency rebuild failed.\n${_header_rebuild_stdout}\n${_header_rebuild_stderr}")
endif()
string(FIND "${_header_rebuild_stdout}${_header_rebuild_stderr}" "Running gentest_codegen" _codegen_rerun_pos)
if(_codegen_rerun_pos EQUAL -1)
  message(FATAL_ERROR "Changing a transitive annotated header did not rerun codegen.\n${_header_rebuild_stdout}\n${_header_rebuild_stderr}")
endif()

set(_cpp_only_header "${_work_dir}/cpp_only_case.hpp")
file(WRITE "${_cpp_only_header}" [=[
#pragma once
#include <gentest/attributes.h>
[[using gentest: test("header_declaration/cpp_only")]]
void cpp_only_case();
]=])
set(_cpp_only_case "${_work_dir}/cpp_only_case.cpp")
file(WRITE "${_cpp_only_case}" [=[
#include <gentest/runner.h>
#include "cpp_only_case.hpp"

[[using gentest: test("header_declaration/cpp_only")]]
void cpp_only_case() {}
]=])
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/cpp_only_generated"
    --textual-registration-output "${_work_dir}/cpp_only_generated/registration.cpp"
    "${_cpp_only_case}"
    --
    -std=c++20
    "-I${GENTEST_SOURCE_DIR}/include"
  RESULT_VARIABLE _cpp_only_rc
  OUTPUT_VARIABLE _cpp_only_stdout
  ERROR_VARIABLE _cpp_only_stderr)
if(_cpp_only_rc EQUAL 0)
  message(FATAL_ERROR "A Gentest attribute in an authored .cpp must be rejected by header-declaration registration.\n${_cpp_only_stdout}\n${_cpp_only_stderr}")
endif()
string(FIND "${_cpp_only_stdout}${_cpp_only_stderr}" "annotate the header declaration instead" _cpp_only_diag_pos)
if(_cpp_only_diag_pos EQUAL -1)
  message(FATAL_ERROR "Expected focused .cpp annotation diagnostic.\n${_cpp_only_stdout}\n${_cpp_only_stderr}")
endif()

set(_conflict_a "${_work_dir}/conflict_a.hpp")
set(_conflict_b "${_work_dir}/conflict_b.hpp")
set(_conflict_source "${_work_dir}/conflict.cpp")
file(WRITE "${_conflict_a}" [=[
#pragma once
#include <gentest/attributes.h>
[[using gentest: test("header_declaration/conflict_a")]]
void conflicting_redeclaration();
]=])
file(WRITE "${_conflict_b}" [=[
#pragma once
#include "conflict_a.hpp"
[[using gentest: test("header_declaration/conflict_b")]]
void conflicting_redeclaration();
]=])
file(WRITE "${_conflict_source}" "#include \"conflict_b.hpp\"\nvoid conflicting_redeclaration() {}\n")
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/conflict_generated"
    --textual-registration-output "${_work_dir}/conflict_generated/registration.cpp"
    "${_conflict_source}"
    --
    -std=c++20
    "-I${GENTEST_SOURCE_DIR}/include"
  RESULT_VARIABLE _conflict_rc
  OUTPUT_VARIABLE _conflict_stdout
  ERROR_VARIABLE _conflict_stderr)
if(_conflict_rc EQUAL 0)
  message(FATAL_ERROR "Conflicting annotated redeclarations unexpectedly succeeded")
endif()
string(FIND "${_conflict_stdout}${_conflict_stderr}" "annotated redeclarations of one entity differ" _conflict_diag_pos)
string(FIND "${_conflict_stdout}${_conflict_stderr}" "conflict_a.hpp" _conflict_a_pos)
string(FIND "${_conflict_stdout}${_conflict_stderr}" "conflict_b.hpp" _conflict_b_pos)
if(_conflict_diag_pos EQUAL -1 OR _conflict_a_pos EQUAL -1 OR _conflict_b_pos EQUAL -1)
  message(FATAL_ERROR "Expected both locations in conflicting-redeclaration diagnostic.\n${_conflict_stdout}\n${_conflict_stderr}")
endif()

set(_duplicate_header "${_work_dir}/duplicate_names.hpp")
set(_duplicate_source "${_work_dir}/duplicate_names.cpp")
file(WRITE "${_duplicate_header}" [=[
#pragma once
#include <gentest/attributes.h>
[[using gentest: test("header_declaration/duplicate")]] void duplicate_name_one();
[[using gentest: test("header_declaration/duplicate")]] void duplicate_name_two();
]=])
file(WRITE "${_duplicate_source}" [=[
#include "duplicate_names.hpp"
void duplicate_name_one() {}
void duplicate_name_two() {}
]=])
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/duplicate_generated"
    --textual-registration-output "${_work_dir}/duplicate_generated/registration.cpp"
    "${_duplicate_source}"
    --
    -std=c++20
    "-I${GENTEST_SOURCE_DIR}/include"
  RESULT_VARIABLE _duplicate_rc
  OUTPUT_VARIABLE _duplicate_stdout
  ERROR_VARIABLE _duplicate_stderr)
if(_duplicate_rc EQUAL 0)
  message(FATAL_ERROR "Two distinct entities with the same case name unexpectedly succeeded")
endif()
string(FIND "${_duplicate_stdout}${_duplicate_stderr}" "duplicate test name" _duplicate_diag_pos)
if(_duplicate_diag_pos EQUAL -1)
  message(FATAL_ERROR "Expected target-wide duplicate-name diagnostic.\n${_duplicate_stdout}\n${_duplicate_stderr}")
endif()

set(_internal_header "${_work_dir}/internal_case.hpp")
set(_internal_source "${_work_dir}/internal_case.cpp")
file(WRITE "${_internal_header}" [=[
#pragma once
#include <gentest/attributes.h>
[[using gentest: test("header_declaration/internal")]]
static inline void internal_case() {}
]=])
file(WRITE "${_internal_source}" "#include \"${_internal_header}\"\n")
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/internal_generated"
    --textual-registration-output "${_work_dir}/internal_generated/registration.cpp"
    "${_internal_source}"
    --
    -std=c++20
    "-I${GENTEST_SOURCE_DIR}/include"
  RESULT_VARIABLE _internal_rc
  OUTPUT_VARIABLE _internal_stdout
  ERROR_VARIABLE _internal_stderr)
if(_internal_rc EQUAL 0)
  message(FATAL_ERROR "Annotated internal-linkage header case unexpectedly succeeded")
endif()
string(FIND "${_internal_stdout}${_internal_stderr}" "require external linkage" _internal_diag_pos)
if(_internal_diag_pos EQUAL -1)
  message(FATAL_ERROR "Expected focused internal-linkage diagnostic.\n${_internal_stdout}\n${_internal_stderr}")
endif()

set(_conditional_header "${_work_dir}/conditional_context.hpp")
file(WRITE "${_conditional_header}" [=[
#pragma once
#include "slot_context.hpp"
#include <gentest/async.h>
#include <gentest/attributes.h>
namespace header_declaration_registration {
#if HEADER_CONTEXT_A
[[using gentest: test("header_declaration/context_conflict")]]
gentest::async_test<void> conditional_context_case();
#else
[[using gentest: test("header_declaration/context_conflict")]]
void conditional_context_case();
#endif
}
]=])
set(GENTEST_SCENARIO_FILE "${_conditional_header}")
set(GENTEST_SCENARIO_CASES_A_PREFIX "#include \"conditional_context.hpp\"")
set(GENTEST_SCENARIO_CASES_B_PREFIX "#include \"conditional_context.hpp\"")
_gentest_expect_fixture_build_failure("conditional_context" "header declaration differs between compile contexts")
unset(GENTEST_SCENARIO_FILE)
unset(GENTEST_SCENARIO_CASES_A_PREFIX)
unset(GENTEST_SCENARIO_CASES_B_PREFIX)

set(_fixture_conflict_header "${_work_dir}/fixture_context.hpp")
file(WRITE "${_fixture_conflict_header}" [=[
#pragma once
#include "slot_context.hpp"
#include <gentest/attributes.h>
namespace header_declaration_registration {
struct [[using gentest: fixture(suite)]] ConditionalFixture {
#if HEADER_CONTEXT_A
  int value;
#else
  long value;
#endif
};
}
]=])
set(GENTEST_SCENARIO_FILE "${_fixture_conflict_header}")
set(GENTEST_SCENARIO_CASES_A_PREFIX "#include \"fixture_context.hpp\"")
set(GENTEST_SCENARIO_CASES_B_PREFIX "#include \"fixture_context.hpp\"")
_gentest_expect_fixture_build_failure("fixture_context" "header fixture declaration differs between compile contexts")
unset(GENTEST_SCENARIO_FILE)
unset(GENTEST_SCENARIO_CASES_A_PREFIX)
unset(GENTEST_SCENARIO_CASES_B_PREFIX)

set(_missing_definition_header "${_work_dir}/missing_definition.hpp")
file(WRITE "${_missing_definition_header}" [=[
#pragma once
#include <gentest/attributes.h>
namespace header_declaration_registration {
[[using gentest: test("header_declaration/missing_definition")]]
void missing_definition_case();
}
]=])
set(GENTEST_SCENARIO_FILE "${_missing_definition_header}")
set(GENTEST_SCENARIO_CASES_A_PREFIX "#include \"missing_definition.hpp\"")
_gentest_expect_fixture_build_failure("missing_definition" "missing_definition_case")
unset(GENTEST_SCENARIO_FILE)
unset(GENTEST_SCENARIO_CASES_A_PREFIX)

set(_non_self_contained_header "${_work_dir}/non_self_contained.hpp")
file(WRITE "${_non_self_contained_header}" [=[
#pragma once
#include <gentest/attributes.h>
namespace header_declaration_registration {
[[using gentest: test("header_declaration/non_self_contained")]]
void non_self_contained_case(IncidentalType &);
}
]=])
set(GENTEST_SCENARIO_FILE "${_non_self_contained_header}")
set(GENTEST_SCENARIO_CASES_A_PREFIX
  "namespace header_declaration_registration { struct IncidentalType {}; }\n#include \"non_self_contained.hpp\"")
_gentest_expect_fixture_build_failure("non_self_contained" "IncidentalType")
unset(GENTEST_SCENARIO_FILE)
unset(GENTEST_SCENARIO_CASES_A_PREFIX)

set(_active_import_header "${_work_dir}/active_import.hpp")
file(WRITE "${_active_import_header}" [=[
#pragma once
import gentest.issue107.unsupported;
#include <gentest/attributes.h>
namespace header_declaration_registration {
[[using gentest: test("header_declaration/active_import")]]
void active_import_case();
}
]=])
set(GENTEST_SCENARIO_FILE "${_active_import_header}")
set(GENTEST_SCENARIO_CASES_A_PREFIX "#include \"active_import.hpp\"")
_gentest_expect_fixture_build_failure("active_import" "does not support textual sources with active named-module imports")
unset(GENTEST_SCENARIO_FILE)
unset(GENTEST_SCENARIO_CASES_A_PREFIX)

message(STATUS "Header-declaration registration regression passed")
