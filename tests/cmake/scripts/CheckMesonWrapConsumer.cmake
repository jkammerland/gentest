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
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Meson wrap consumer check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Meson wrap consumer check.")
  return()
endif()

find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20)
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

set(_meson_env
  "CC=${_clang_cc}"
  "CXX=${_clang_cxx}"
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
    "${_meson_textual_dir}/tu_0000_downstream_textual_mocks_defs.gentest.h"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_registry.hpp"
    "${_meson_textual_dir}/downstream_textual_mocks_mock_impl.hpp")
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
    "${_meson_textual_dir}/downstream_textual_mocks_anchor.cpp"
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
