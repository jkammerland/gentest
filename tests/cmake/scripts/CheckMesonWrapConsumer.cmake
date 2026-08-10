if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckMesonWrapConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMesonWrapConsumer.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckMesonWrapConsumer.cmake: PROG not set")
endif()

if(WIN32)
  message(STATUS "Skipping Meson wrap consumer check on Windows.")
  return()
endif()

find_program(_meson NAMES meson)
if(NOT _meson)
  message(STATUS "GENTEST_SKIP_TEST: meson not found")
  return()
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckMesonWrapConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

set(_gentest_clang_search_paths
  /usr/lib64/llvm23/bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-23/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

find_program(_clang_cxx NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++-19 clang++)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Meson wrap consumer check.")
  return()
endif()

find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang-19 clang)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Meson wrap consumer check.")
  return()
endif()

find_program(_clang_scan_deps NAMES clang-scan-deps-23 clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20 clang-scan-deps-19 clang-scan-deps
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps NAMES clang-scan-deps-23 clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20 clang-scan-deps-19 clang-scan-deps)
endif()

string(MD5 _scratch_hash "${BUILD_ROOT}")
set(_scratch_base "")
foreach(_candidate IN ITEMS "/tmp" "/dev/shm")
  if(EXISTS "${_candidate}")
    set(_probe_dir "${_candidate}/gentest_meson_wrap_probe_${_scratch_hash}")
    set(_probe_file "${_probe_dir}/probe_true")
    file(MAKE_DIRECTORY "${_probe_dir}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different /bin/true "${_probe_file}"
      RESULT_VARIABLE _probe_copy_rc
      OUTPUT_QUIET
      ERROR_QUIET)
    if(_probe_copy_rc EQUAL 0)
      execute_process(
        COMMAND chmod +x "${_probe_file}"
        RESULT_VARIABLE _probe_chmod_rc
        OUTPUT_QUIET
        ERROR_QUIET)
      if(_probe_chmod_rc EQUAL 0)
        execute_process(
          COMMAND "${_probe_file}"
          RESULT_VARIABLE _probe_run_rc
          OUTPUT_QUIET
          ERROR_QUIET)
      else()
        set(_probe_run_rc 1)
      endif()
      file(REMOVE_RECURSE "${_probe_dir}")
      if(_probe_run_rc EQUAL 0)
        set(_scratch_base "${_candidate}")
        break()
      endif()
    else()
      file(REMOVE_RECURSE "${_probe_dir}")
    endif()
  endif()
endforeach()
if("${_scratch_base}" STREQUAL "")
  foreach(_candidate IN ITEMS "/tmp" "/dev/shm")
    if(EXISTS "${_candidate}")
      set(_probe_file "${_candidate}/gentest_meson_wrap_probe_${_scratch_hash}")
      execute_process(
        COMMAND "${CMAKE_COMMAND}" -E touch "${_probe_file}"
        RESULT_VARIABLE _probe_rc
        OUTPUT_QUIET
        ERROR_QUIET)
      if(_probe_rc EQUAL 0)
        file(REMOVE "${_probe_file}")
        set(_scratch_base "${_candidate}")
        break()
      endif()
    endif()
  endforeach()
endif()
if("${_scratch_base}" STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: no writable scratch root available for Meson wrap consumer check")
  return()
endif()

set(_scratch_root "${_scratch_base}/gentest_meson_wrap_consumer_${_scratch_hash}")
file(REMOVE_RECURSE "${_scratch_root}")
file(MAKE_DIRECTORY "${_scratch_root}")
file(MAKE_DIRECTORY "${_scratch_root}/workspace/subprojects")
file(MAKE_DIRECTORY "${_scratch_root}/workspace/subprojects/gentest")
file(MAKE_DIRECTORY "${_scratch_root}/workspace/tmp")

file(COPY
  "${SOURCE_DIR}/tests/downstream/meson_wrap_consumer/meson.build"
  "${SOURCE_DIR}/tests/downstream/meson_wrap_consumer/meson_options.txt"
  DESTINATION "${_scratch_root}/workspace")
file(COPY
  "${SOURCE_DIR}/tests/downstream/meson_wrap_consumer/tests"
  DESTINATION "${_scratch_root}/workspace")

set(_subproject_root "${_scratch_root}/workspace/subprojects/gentest")
file(COPY "${SOURCE_DIR}/meson.build" DESTINATION "${_subproject_root}")
file(COPY "${SOURCE_DIR}/meson_options.txt" DESTINATION "${_subproject_root}")
file(COPY "${SOURCE_DIR}/meson" DESTINATION "${_subproject_root}")
file(COPY "${SOURCE_DIR}/include" DESTINATION "${_subproject_root}")
file(COPY "${SOURCE_DIR}/src" DESTINATION "${_subproject_root}")
file(MAKE_DIRECTORY "${_subproject_root}/tests")
file(MAKE_DIRECTORY "${_subproject_root}/third_party")
file(COPY "${SOURCE_DIR}/third_party/include" DESTINATION "${_subproject_root}/third_party")

set(_out_dir "${_scratch_root}/workspace/build")

# Keep the default-option proof deterministic: false emits no cache CLI option,
# so a caller's ambient cache policy would otherwise still apply.
set(_meson_env
  "CC=${_clang_cc}"
  "CXX=${_clang_cxx}"
  "GENTEST_CODEGEN_PARSE_CACHE=OFF"
  "GENTEST_CODEGEN_PARSE_CACHE_DIR="
  "TMPDIR=${_scratch_root}/workspace/tmp")

set(_setup_args
  setup "${_out_dir}" "${_scratch_root}/workspace" "--wipe"
  "-Dgentest_codegen_path=${_codegen}"
  "-Dgentest_codegen_host_clang=${_clang_cxx}")
if(_clang_scan_deps)
  list(APPEND _setup_args "-Dgentest_codegen_clang_scan_deps=${_clang_scan_deps}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" ${_setup_args}
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _setup_rc
  OUTPUT_VARIABLE _setup_out
  ERROR_VARIABLE _setup_err)
if(NOT _setup_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson wrap consumer setup failed.\n"
    "stdout:\n${_setup_out}\n"
    "stderr:\n${_setup_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v gentest_downstream_textual_mocks
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _mock_build_rc
  OUTPUT_VARIABLE _mock_build_out
  ERROR_VARIABLE _mock_build_err)
if(NOT _mock_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson wrap consumer mock target compile failed.\n"
    "stdout:\n${_mock_build_out}\n"
    "stderr:\n${_mock_build_err}")
endif()

set(_meson_textual_dir "${_out_dir}/subprojects/gentest/meson/textual")
foreach(_expected_mock_file IN ITEMS
    "${_meson_textual_dir}/downstream_textual_mocks_defs.cpp"
    "${_meson_textual_dir}/tu_0000_downstream_textual_mocks_defs.gentest.h"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_registry.hpp"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_impl.hpp"
    "${_meson_textual_dir}/gentest_downstream_mocks.hpp")
  if(NOT EXISTS "${_expected_mock_file}")
    message(FATAL_ERROR
      "Building the Meson mock target alone did not produce expected artifact '${_expected_mock_file}'.\n"
      "stdout:\n${_mock_build_out}\n"
      "stderr:\n${_mock_build_err}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson wrap consumer compile failed.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

foreach(_expected_file IN ITEMS
    "${_meson_textual_dir}/downstream_textual_mocks_defs.cpp"
    "${_meson_textual_dir}/tu_0000_downstream_textual_mocks_defs.gentest.h"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_registry.hpp"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_impl.hpp"
    "${_meson_textual_dir}/gentest_downstream_mocks.hpp"
    "${_meson_textual_dir}/tu_0000_downstream_textual_cases.gentest.h"
    "${_meson_textual_dir}/gentest_downstream_textual.artifact_manifest.json")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR
      "Meson wrap consumer build did not produce expected artifact '${_expected_file}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

set(_combined_build_log "${_mock_build_out}\n${_mock_build_err}\n${_build_out}\n${_build_err}")
string(FIND "${_combined_build_log}" "--parse-cache-dir" _default_parse_cache_pos)
if(NOT _default_parse_cache_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson default codegen unexpectedly enabled the textual parse cache.\n"
    "mock stdout:\n${_mock_build_out}\n"
    "mock stderr:\n${_mock_build_err}\n"
    "suite stdout:\n${_build_out}\n"
    "suite stderr:\n${_build_err}")
endif()
foreach(_expected_depfile_flag IN ITEMS
    "--depfile"
    "--artifact-manifest"
    "--artifact-owner-source"
    "--compile-context-id"
    "tu_0000_downstream_textual_mocks_defs.gentest.h.d"
    "tu_0000_downstream_textual_cases.gentest.h.d")
  string(FIND "${_combined_build_log}" "${_expected_depfile_flag}" _depfile_flag_pos)
  if(_depfile_flag_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson wrap consumer build did not pass expected depfile argument '${_expected_depfile_flag}'.\n"
      "mock stdout:\n${_mock_build_out}\n"
      "mock stderr:\n${_mock_build_err}\n"
      "suite stdout:\n${_build_out}\n"
      "suite stderr:\n${_build_err}")
  endif()
endforeach()

# The custom targets intentionally depend directly on their primary sources
# (and generated mock target), not on a configure-time glob of all headers.
# Ninja's depfile database owns the precise header edges. Inspect planned
# edges, rather than elapsed time, so this remains reliable on contended hosts.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _no_work_rc
  OUTPUT_VARIABLE _no_work_out
  ERROR_VARIABLE _no_work_err)
if(NOT _no_work_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson no-op dry run failed.\n"
    "stdout:\n${_no_work_out}\n"
    "stderr:\n${_no_work_err}")
endif()
set(_no_work_log "${_no_work_out}\n${_no_work_err}")
string(FIND "${_no_work_log}" "ninja: no work to do." _no_work_pos)
if(_no_work_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson no-op dry run unexpectedly planned codegen, compilation, or linking.\n"
    "stdout:\n${_no_work_out}\n"
    "stderr:\n${_no_work_err}")
endif()

set(_fixture_tests_dir "${_scratch_root}/workspace/tests")
file(APPEND "${_fixture_tests_dir}/unrelated_header.hpp" "\n// unrelated edit must not invalidate codegen\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _unrelated_plan_rc
  OUTPUT_VARIABLE _unrelated_plan_out
  ERROR_VARIABLE _unrelated_plan_err)
if(NOT _unrelated_plan_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson unrelated-header dry run failed.\n"
    "stdout:\n${_unrelated_plan_out}\n"
    "stderr:\n${_unrelated_plan_err}")
endif()
set(_unrelated_plan_log "${_unrelated_plan_out}\n${_unrelated_plan_err}")
string(FIND "${_unrelated_plan_log}" "ninja: no work to do." _unrelated_no_work_pos)
if(_unrelated_no_work_pos EQUAL -1)
  message(FATAL_ERROR
    "Editing an unrelated known header unexpectedly invalidated Meson codegen.\n"
    "stdout:\n${_unrelated_plan_out}\n"
    "stderr:\n${_unrelated_plan_err}")
endif()

# A successful __has_include probe affects generated annotations even when the
# discovered file is never included. Clang's preprocessing callback must place
# that positive probe in the codegen depfile so removing it schedules codegen.
file(REMOVE "${_fixture_tests_dir}/positive_has_include.hpp")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _has_include_plan_rc
  OUTPUT_VARIABLE _has_include_plan_out
  ERROR_VARIABLE _has_include_plan_err)
if(NOT _has_include_plan_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson positive __has_include removal dry run failed.\n"
    "stdout:\n${_has_include_plan_out}\n"
    "stderr:\n${_has_include_plan_err}")
endif()
set(_has_include_plan_log "${_has_include_plan_out}\n${_has_include_plan_err}")
foreach(_has_include_expected IN ITEMS
    "gentest_codegen"
    "tests/cases.cpp"
    " -c "
    " -o ")
  string(FIND "${_has_include_plan_log}" "${_has_include_expected}" _has_include_expected_pos)
  if(_has_include_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Removing a positive __has_include probe did not plan expected edge '${_has_include_expected}'.\n"
      "stdout:\n${_has_include_plan_out}\n"
      "stderr:\n${_has_include_plan_err}")
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -j 1 -v gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _has_include_build_rc
  OUTPUT_VARIABLE _has_include_build_out
  ERROR_VARIABLE _has_include_build_err)
if(NOT _has_include_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson positive __has_include removal rebuild failed.\n"
    "stdout:\n${_has_include_build_out}\n"
    "stderr:\n${_has_include_build_err}")
endif()
set(_has_include_consumer_bin "${_meson_textual_dir}/gentest_downstream_textual")
execute_process(
  COMMAND "${_has_include_consumer_bin}" --list
  RESULT_VARIABLE _has_include_list_rc
  OUTPUT_VARIABLE _has_include_list_out
  ERROR_VARIABLE _has_include_list_err)
if(NOT _has_include_list_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson positive __has_include removal listing failed.\n"
    "stdout:\n${_has_include_list_out}\n"
    "stderr:\n${_has_include_list_err}")
endif()
string(FIND "${_has_include_list_out}" "downstream/positive_has_include" _stale_has_include_case_pos)
if(NOT _stale_has_include_case_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson rebuild retained the removed positive __has_include case.\n${_has_include_list_out}")
endif()

file(APPEND "${_fixture_tests_dir}/private_case_value.hpp" "\n// used private-header edit\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _private_plan_rc
  OUTPUT_VARIABLE _private_plan_out
  ERROR_VARIABLE _private_plan_err)
if(NOT _private_plan_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson private-header dry run failed.\n"
    "stdout:\n${_private_plan_out}\n"
    "stderr:\n${_private_plan_err}")
endif()
set(_private_plan_log "${_private_plan_out}\n${_private_plan_err}")
foreach(_private_expected IN ITEMS
    "gentest_codegen"
    "tests/cases.cpp"
    " -c "
    " -o ")
  string(FIND "${_private_plan_log}" "${_private_expected}" _private_expected_pos)
  if(_private_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Used private-header edit did not plan expected downstream edge '${_private_expected}'.\n"
      "stdout:\n${_private_plan_out}\n"
      "stderr:\n${_private_plan_err}")
  endif()
endforeach()
string(FIND "${_private_plan_log}" "header_mock_defs.hpp" _private_mock_codegen_pos)
if(NOT _private_mock_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "A private suite-header edit unexpectedly planned mock codegen.\n"
    "stdout:\n${_private_plan_out}\n"
    "stderr:\n${_private_plan_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -j 1 -v gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _private_build_rc
  OUTPUT_VARIABLE _private_build_out
  ERROR_VARIABLE _private_build_err)
if(NOT _private_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson private-header rebuild failed.\n"
    "stdout:\n${_private_build_out}\n"
    "stderr:\n${_private_build_err}")
endif()

file(APPEND "${_fixture_tests_dir}/shared_service_value.hpp" "\n// used shared-header edit\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _shared_plan_rc
  OUTPUT_VARIABLE _shared_plan_out
  ERROR_VARIABLE _shared_plan_err)
if(NOT _shared_plan_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson shared-header dry run failed.\n"
    "stdout:\n${_shared_plan_out}\n"
    "stderr:\n${_shared_plan_err}")
endif()
set(_shared_plan_log "${_shared_plan_out}\n${_shared_plan_err}")
foreach(_shared_expected IN ITEMS
    "gentest_codegen"
    "header_mock_defs.hpp"
    "tests/cases.cpp"
    " -c "
    " -o ")
  string(FIND "${_shared_plan_log}" "${_shared_expected}" _shared_expected_pos)
  if(_shared_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Used shared-header edit did not plan expected downstream edge '${_shared_expected}'.\n"
      "stdout:\n${_shared_plan_out}\n"
      "stderr:\n${_shared_plan_err}")
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_out_dir}" -j 1 -v gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _shared_build_rc
  OUTPUT_VARIABLE _shared_build_out
  ERROR_VARIABLE _shared_build_err)
if(NOT _shared_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson shared-header rebuild failed.\n"
    "stdout:\n${_shared_build_out}\n"
    "stderr:\n${_shared_build_err}")
endif()

# Exercise the configured cache flag in a separate build. The timing sidecars
# are added only to direct invocations of the exact Ninja command, keeping the
# Meson output contract unchanged while proving an empty cache misses and its
# immediate repeat hits.
find_program(_sh NAMES sh)
if(NOT _sh)
  message(FATAL_ERROR "Meson parse-cache regression requires a POSIX shell")
endif()
set(_cache_dir "${_scratch_root}/workspace/meson-parse-cache")
set(_cache_out_dir "${_scratch_root}/workspace/cache-build")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" setup "${_cache_out_dir}" "${_scratch_root}/workspace" "--wipe"
          "-Dgentest_codegen_path=${_codegen}"
          "-Dgentest_codegen_host_clang=${_clang_cxx}"
          "-Dgentest_codegen_parse_cache=true"
          "-Dgentest_codegen_parse_cache_dir=${_cache_dir}"
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _cache_setup_rc
  OUTPUT_VARIABLE _cache_setup_out
  ERROR_VARIABLE _cache_setup_err)
if(NOT _cache_setup_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson parse-cache setup failed.\n"
    "stdout:\n${_cache_setup_out}\n"
    "stderr:\n${_cache_setup_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_cache_out_dir}" -j 1 -v
          gentest_downstream_cache_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _cache_build_rc
  OUTPUT_VARIABLE _cache_build_out
  ERROR_VARIABLE _cache_build_err)
if(NOT _cache_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson parse-cache build failed.\n"
    "stdout:\n${_cache_build_out}\n"
    "stderr:\n${_cache_build_err}")
endif()
set(_cache_build_log "${_cache_build_out}\n${_cache_build_err}")
set(_expected_cache_arg "--parse-cache-dir ${_cache_dir}")
string(FIND "${_cache_build_log}" "${_expected_cache_arg}" _cache_arg_pos)
if(_cache_arg_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson parse-cache option was not forwarded exactly as '${_expected_cache_arg}'.\n"
    "stdout:\n${_cache_build_out}\n"
    "stderr:\n${_cache_build_err}")
endif()
file(GLOB _cache_entries "${_cache_dir}/*")
if("${_cache_entries}" STREQUAL "")
  message(FATAL_ERROR "Meson parse-cache build did not create an entry in '${_cache_dir}'")
endif()

# Build the established consumer once without the flag so the reconfigure below
# changes only Meson arguments. Its conditional annotation starts as
# downstream/compile_flag_off and must become downstream/compile_flag.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_cache_out_dir}" -j 1 -v
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _baseline_flag_build_rc
  OUTPUT_VARIABLE _baseline_flag_build_out
  ERROR_VARIABLE _baseline_flag_build_err)
if(NOT _baseline_flag_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson baseline compile-flag consumer build failed.\n"
    "stdout:\n${_baseline_flag_build_out}\n"
    "stderr:\n${_baseline_flag_build_err}")
endif()
set(_baseline_consumer_bin "${_cache_out_dir}/subprojects/gentest/meson/textual/gentest_downstream_textual")
execute_process(
  COMMAND "${_baseline_consumer_bin}" --list
  RESULT_VARIABLE _baseline_flag_list_rc
  OUTPUT_VARIABLE _baseline_flag_list_out
  ERROR_VARIABLE _baseline_flag_list_err)
if(NOT _baseline_flag_list_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson baseline compile-flag consumer listing failed.\n"
    "stdout:\n${_baseline_flag_list_out}\n"
    "stderr:\n${_baseline_flag_list_err}")
endif()
string(FIND "${_baseline_flag_list_out}" "downstream/compile_flag_off" _baseline_flag_case_pos)
if(_baseline_flag_case_pos EQUAL -1)
  message(FATAL_ERROR "Meson baseline consumer did not generate downstream/compile_flag_off.\n${_baseline_flag_list_out}")
endif()

# A build-definition reconfigure that changes parser and compiler flags must
# invalidate the already-built custom command and regenerate its wrapper.
set(_fixture_meson_build "${_scratch_root}/workspace/meson.build")
file(READ "${_fixture_meson_build}" _fixture_meson_text)
set(_consumer_suite_marker "'test_name': 'meson_downstream_textual',")
set(_consumer_suite_marker_with_flag
    "'clang_args': ['-DDOWNSTREAM_MESON_CODEGEN_FLAG=1'],\n    'cpp_args': ['-DDOWNSTREAM_MESON_CODEGEN_FLAG=1'],\n    'test_name': 'meson_downstream_textual',")
string(FIND "${_fixture_meson_text}" "${_consumer_suite_marker}" _consumer_suite_marker_pos)
if(_consumer_suite_marker_pos EQUAL -1)
  message(FATAL_ERROR "Meson compile-flag regression could not locate the consumer suite declaration")
endif()
string(REPLACE "${_consumer_suite_marker}" "${_consumer_suite_marker_with_flag}" _fixture_meson_text "${_fixture_meson_text}")
file(WRITE "${_fixture_meson_build}" "${_fixture_meson_text}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" setup "${_cache_out_dir}" "${_scratch_root}/workspace" "--reconfigure"
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _flag_reconfigure_rc
  OUTPUT_VARIABLE _flag_reconfigure_out
  ERROR_VARIABLE _flag_reconfigure_err)
if(NOT _flag_reconfigure_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson compile-flag reconfigure failed.\n"
    "stdout:\n${_flag_reconfigure_out}\n"
    "stderr:\n${_flag_reconfigure_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_cache_out_dir}" -v "--ninja-args=-n"
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _flag_plan_rc
  OUTPUT_VARIABLE _flag_plan_out
  ERROR_VARIABLE _flag_plan_err)
if(NOT _flag_plan_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson compile-flag dry run failed.\n"
    "stdout:\n${_flag_plan_out}\n"
    "stderr:\n${_flag_plan_err}")
endif()
set(_flag_plan_log "${_flag_plan_out}\n${_flag_plan_err}")
foreach(_flag_expected IN ITEMS
    "gentest_codegen"
    "-DDOWNSTREAM_MESON_CODEGEN_FLAG=1"
    "tests/cases.cpp"
    " -c "
    " -o ")
  string(FIND "${_flag_plan_log}" "${_flag_expected}" _flag_expected_pos)
  if(_flag_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson compile-flag change did not plan expected edge '${_flag_expected}'.\n"
      "stdout:\n${_flag_plan_out}\n"
      "stderr:\n${_flag_plan_err}")
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_meson}" compile -C "${_cache_out_dir}" -j 1 -v
          gentest_downstream_textual
  WORKING_DIRECTORY "${_scratch_root}/workspace"
  RESULT_VARIABLE _flag_build_rc
  OUTPUT_VARIABLE _flag_build_out
  ERROR_VARIABLE _flag_build_err)
if(NOT _flag_build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson compile-flag rebuild failed.\n"
    "stdout:\n${_flag_build_out}\n"
    "stderr:\n${_flag_build_err}")
endif()
set(_cache_consumer_bin "${_cache_out_dir}/subprojects/gentest/meson/textual/gentest_downstream_textual")
execute_process(
  COMMAND "${_cache_consumer_bin}" --list
  RESULT_VARIABLE _cache_list_rc
  OUTPUT_VARIABLE _cache_list_out
  ERROR_VARIABLE _cache_list_err)
if(NOT _cache_list_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson compile-flag consumer suite listing failed.\n"
    "stdout:\n${_cache_list_out}\n"
    "stderr:\n${_cache_list_err}")
endif()
string(FIND "${_cache_list_out}" "downstream/compile_flag" _cache_flag_case_pos)
if(_cache_flag_case_pos EQUAL -1)
  message(FATAL_ERROR "Meson compile-flag rebuild did not generate downstream/compile_flag.\n${_cache_list_out}")
endif()
string(FIND "${_cache_list_out}" "downstream/compile_flag_off" _cache_stale_case_pos)
if(NOT _cache_stale_case_pos EQUAL -1)
  message(FATAL_ERROR "Meson compile-flag rebuild retained stale downstream/compile_flag_off.\n${_cache_list_out}")
endif()

function(_gentest_extract_meson_suite_codegen_command build_ninja source_suffix output_var)
  file(STRINGS "${build_ninja}" _ninja_lines)
  foreach(_ninja_line IN LISTS _ninja_lines)
    string(FIND "${_ninja_line}" "COMMAND = " _command_pos)
    string(FIND "${_ninja_line}" "gentest_codegen" _codegen_pos)
    string(FIND "${_ninja_line}" "--artifact-owner-source" _owner_pos)
    string(FIND "${_ninja_line}" "${source_suffix}" _source_pos)
    if(NOT _command_pos EQUAL -1 AND NOT _codegen_pos EQUAL -1 AND NOT _owner_pos EQUAL -1 AND NOT _source_pos EQUAL -1)
      string(REGEX REPLACE "^[ \t]*COMMAND = " "" _command "${_ninja_line}")
      set(${output_var} "${_command}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "Could not find the downstream textual suite codegen command in '${build_ninja}'")
endfunction()

function(_gentest_add_timing_json codegen_command timing_path output_var)
  string(FIND "${codegen_command}" " -- " _separator_pos)
  if(_separator_pos EQUAL -1)
    message(FATAL_ERROR "Meson codegen command is missing its compiler-argument separator: ${codegen_command}")
  endif()
  string(REPLACE " -- " " --timing-json ${timing_path} -- " _timed_command "${codegen_command}")
  set(${output_var} "${_timed_command}" PARENT_SCOPE)
endfunction()

_gentest_extract_meson_suite_codegen_command("${_out_dir}/build.ninja" "/tests/cases.cpp" _default_suite_codegen_command)
_gentest_add_timing_json("${_default_suite_codegen_command}" "${_scratch_root}/workspace/default-cache-state.json"
                          _default_timed_command)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_sh}" -c "${_default_timed_command}"
  WORKING_DIRECTORY "${_out_dir}"
  RESULT_VARIABLE _default_timed_rc
  OUTPUT_VARIABLE _default_timed_out
  ERROR_VARIABLE _default_timed_err)
if(NOT _default_timed_rc EQUAL 0)
  message(FATAL_ERROR
    "Direct default Meson codegen command failed.\n"
    "stdout:\n${_default_timed_out}\n"
    "stderr:\n${_default_timed_err}")
endif()
file(READ "${_scratch_root}/workspace/default-cache-state.json" _default_timing_json)
string(FIND "${_default_timing_json}" "\"cache\":" _default_cache_state_pos)
if(NOT _default_cache_state_pos EQUAL -1)
  message(FATAL_ERROR "Default Meson codegen command unexpectedly recorded a cache state. Timing JSON:\n${_default_timing_json}")
endif()

_gentest_extract_meson_suite_codegen_command("${_cache_out_dir}/build.ninja" "/tests/cache_cases.cpp" _cache_suite_codegen_command)
set(_controlled_cache_dir "${_scratch_root}/workspace/controlled-parse-cache")
string(REPLACE "${_cache_dir}" "${_controlled_cache_dir}" _controlled_cache_command "${_cache_suite_codegen_command}")
string(FIND "${_controlled_cache_command}" "${_controlled_cache_dir}" _controlled_cache_arg_pos)
if(_controlled_cache_arg_pos EQUAL -1)
  message(FATAL_ERROR "Could not retarget the configured Meson parse-cache command")
endif()
_gentest_add_timing_json("${_controlled_cache_command}" "${_scratch_root}/workspace/cache-miss.json" _cache_miss_command)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_sh}" -c "${_cache_miss_command}"
  WORKING_DIRECTORY "${_cache_out_dir}"
  RESULT_VARIABLE _cache_miss_rc
  OUTPUT_VARIABLE _cache_miss_out
  ERROR_VARIABLE _cache_miss_err)
if(NOT _cache_miss_rc EQUAL 0)
  message(FATAL_ERROR
    "Controlled Meson parse-cache miss command failed.\n"
    "stdout:\n${_cache_miss_out}\n"
    "stderr:\n${_cache_miss_err}")
endif()
file(READ "${_scratch_root}/workspace/cache-miss.json" _cache_miss_timing_json)
string(FIND "${_cache_miss_timing_json}" "\"cache\": \"miss\"" _cache_miss_pos)
if(_cache_miss_pos EQUAL -1)
  message(FATAL_ERROR "Controlled Meson parse-cache regeneration did not miss. Timing JSON:\n${_cache_miss_timing_json}")
endif()
_gentest_add_timing_json("${_controlled_cache_command}" "${_scratch_root}/workspace/cache-hit.json" _cache_hit_command)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_meson_env} "${_sh}" -c "${_cache_hit_command}"
  WORKING_DIRECTORY "${_cache_out_dir}"
  RESULT_VARIABLE _cache_hit_rc
  OUTPUT_VARIABLE _cache_hit_out
  ERROR_VARIABLE _cache_hit_err)
if(NOT _cache_hit_rc EQUAL 0)
  message(FATAL_ERROR
    "Controlled Meson parse-cache hit command failed.\n"
    "stdout:\n${_cache_hit_out}\n"
    "stderr:\n${_cache_hit_err}")
endif()
file(READ "${_scratch_root}/workspace/cache-hit.json" _cache_hit_timing_json)
string(FIND "${_cache_hit_timing_json}" "\"cache\": \"hit\"" _cache_hit_pos)
if(_cache_hit_pos EQUAL -1)
  message(FATAL_ERROR "Controlled Meson parse-cache regeneration did not hit. Timing JSON:\n${_cache_hit_timing_json}")
endif()

set(_textual_manifest "${_meson_textual_dir}/gentest_downstream_textual.artifact_manifest.json")
file(READ "${_textual_manifest}" _textual_manifest_json)
foreach(_expected_manifest_token IN ITEMS
    "\"kind\": \"textual-wrapper\""
    "\"role\": \"registration\""
    "\"compile_as\": \"cxx-textual-wrapper\""
    "\"target_attachment\": \"replace-owner-source\""
    "\"includes_owner_source\": true"
    "\"replaces_owner_source\": true"
    "\"requires_module_scan\": false")
  string(FIND "${_textual_manifest_json}" "${_expected_manifest_token}" _manifest_token_pos)
  if(_manifest_token_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson wrap consumer textual artifact manifest is missing '${_expected_manifest_token}'.\n"
      "${_textual_manifest_json}")
  endif()
endforeach()

set(_consumer_bin "${_meson_textual_dir}/gentest_downstream_textual")
if(NOT EXISTS "${_consumer_bin}")
  message(FATAL_ERROR "Expected built Meson wrap consumer binary was not found: ${_consumer_bin}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson wrap consumer listing failed.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_expected IN ITEMS
    "downstream/textual_test"
    "downstream/textual_mock"
    "downstream/textual_log_sink"
    "downstream/textual_bench"
    "downstream/textual_jitter")
  string(FIND "${_list_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson wrap consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --run=downstream/textual_test --kind=test
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson wrap consumer test failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=downstream/textual_mock --kind=test
  RESULT_VARIABLE _mock_rc
  OUTPUT_VARIABLE _mock_out
  ERROR_VARIABLE _mock_err)
if(NOT _mock_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson wrap consumer mock test failed.\n"
    "stdout:\n${_mock_out}\n"
    "stderr:\n${_mock_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=downstream/textual_log_sink --kind=test
  RESULT_VARIABLE _log_sink_rc
  OUTPUT_VARIABLE _log_sink_out
  ERROR_VARIABLE _log_sink_err)
if(NOT _log_sink_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson wrap consumer log sink test failed.\n"
    "stdout:\n${_log_sink_out}\n"
    "stderr:\n${_log_sink_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=downstream/textual_bench --kind=bench
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson wrap consumer bench failed.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=downstream/textual_jitter --kind=jitter
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson wrap consumer jitter failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
