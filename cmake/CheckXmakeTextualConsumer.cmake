if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: PROG not set")
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeTextualConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

function(_gentest_resolve_xmake_test_tool out_var raw_value label)
  if("${raw_value}" STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  if(IS_ABSOLUTE "${raw_value}")
    if(NOT EXISTS "${raw_value}")
      message(FATAL_ERROR
        "CheckXmakeTextualConsumer.cmake: ${label} does not exist: ${raw_value}")
    endif()
    set(${out_var} "${raw_value}" PARENT_SCOPE)
    return()
  endif()

  find_program(_resolved_tool NAMES "${raw_value}")
  if(NOT _resolved_tool)
    message(FATAL_ERROR
      "CheckXmakeTextualConsumer.cmake: failed to resolve ${label} from '${raw_value}'.")
  endif()
  set(${out_var} "${_resolved_tool}" PARENT_SCOPE)
endfunction()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

set(_gentest_xmake_root "${CMAKE_CURRENT_BINARY_DIR}")
if(DEFINED BUILD_ROOT AND NOT "${BUILD_ROOT}" STREQUAL "")
  set(_gentest_xmake_root "${BUILD_ROOT}")
endif()

set(_project_dir "${SOURCE_DIR}")
if(WIN32)
  set(_project_dir "${_gentest_xmake_root}/xw")
  file(REMOVE_RECURSE "${_project_dir}")
  file(MAKE_DIRECTORY "${_project_dir}")
  file(COPY "${SOURCE_DIR}/include" DESTINATION "${_project_dir}")
  file(COPY "${SOURCE_DIR}/src" DESTINATION "${_project_dir}")
  file(COPY "${SOURCE_DIR}/tests" DESTINATION "${_project_dir}")
  file(COPY "${SOURCE_DIR}/xmake" DESTINATION "${_project_dir}")
  file(COPY "${SOURCE_DIR}/third_party/include" DESTINATION "${_project_dir}/third_party")
  file(COPY_FILE "${SOURCE_DIR}/xmake.lua" "${_project_dir}/xmake.lua")
endif()

set(_gentest_clang_search_paths "")
foreach(_compiler_path IN ITEMS "${CXX_COMPILER}" "${C_COMPILER}")
  if(_compiler_path)
    get_filename_component(_compiler_realpath "${_compiler_path}" REALPATH)
    if(_compiler_realpath)
      get_filename_component(_compiler_dir "${_compiler_realpath}" DIRECTORY)
    else()
      get_filename_component(_compiler_dir "${_compiler_path}" DIRECTORY)
    endif()
    list(APPEND _gentest_clang_search_paths "${_compiler_dir}")
  endif()
endforeach()
foreach(_cmake_dir IN ITEMS "${Clang_DIR}" "${LLVM_DIR}")
  if(_cmake_dir)
    get_filename_component(_llvm_prefix "${_cmake_dir}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    list(APPEND _gentest_clang_search_paths "${_llvm_prefix}/bin")
  endif()
endforeach()
if(APPLE)
  list(APPEND _gentest_clang_search_paths
    /opt/homebrew/opt/llvm@22/bin
    /opt/homebrew/opt/llvm@21/bin
    /opt/homebrew/opt/llvm@20/bin
    /usr/local/opt/llvm/bin)
endif()
list(APPEND _gentest_clang_search_paths
  /usr/bin
  /bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin)
list(REMOVE_DUPLICATES _gentest_clang_search_paths)

find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake textual consumer smoke check.")
  return()
endif()

set(_target_cxx "${_clang_cxx}")
set(_target_cc "${_clang_cc}")
set(_configured_target_cc "$ENV{GENTEST_XMAKE_TEST_TARGET_CC}")
set(_configured_target_cxx "$ENV{GENTEST_XMAKE_TEST_TARGET_CXX}")
set(_has_configured_target_cc FALSE)
set(_has_configured_target_cxx FALSE)
if(NOT "${_configured_target_cc}" STREQUAL "")
  set(_has_configured_target_cc TRUE)
endif()
if(NOT "${_configured_target_cxx}" STREQUAL "")
  set(_has_configured_target_cxx TRUE)
endif()
if(_has_configured_target_cc AND NOT _has_configured_target_cxx)
  message(FATAL_ERROR
    "CheckXmakeTextualConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
if(_has_configured_target_cxx AND NOT _has_configured_target_cc)
  message(FATAL_ERROR
    "CheckXmakeTextualConsumer.cmake: GENTEST_XMAKE_TEST_TARGET_CC and "
    "GENTEST_XMAKE_TEST_TARGET_CXX must be set together.")
endif()
if(NOT "${_configured_target_cc}" STREQUAL "")
  _gentest_resolve_xmake_test_tool(_target_cc "${_configured_target_cc}" "GENTEST_XMAKE_TEST_TARGET_CC")
  _gentest_resolve_xmake_test_tool(_target_cxx "${_configured_target_cxx}" "GENTEST_XMAKE_TEST_TARGET_CXX")
endif()

set(_out_dir "${_gentest_xmake_root}/tmp_xmake_textual_consumer")
set(_xmake_global_dir "${_gentest_xmake_root}/xg")
if(WIN32)
  set(_out_dir "${_project_dir}/b")
endif()
file(REMOVE_RECURSE "${_out_dir}")
file(REMOVE_RECURSE "${_xmake_global_dir}")
file(MAKE_DIRECTORY "${_out_dir}/tmp")

set(_xmake_env
  "GENTEST_CODEGEN=${_codegen}"
  "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
  "GENTEST_XMAKE_SKIP_MODULE_TARGETS=1"
  "CC=${_target_cc}"
  "CXX=${_target_cxx}"
  "TMPDIR=${_out_dir}/tmp"
  "XMAKE_GLOBALDIR=${_xmake_global_dir}")

set(_xmake_config_args
  f -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  "--cc=${_target_cc}"
  "--cxx=${_target_cxx}")
set(_xmake_build_args
  build -P "${_project_dir}" -F "${_project_dir}/xmake.lua" -y -vD)
if(WIN32)
  set(_xmake_config_args
    f -P . -F xmake.lua -o b -m debug -c -y
    "--cc=${_target_cc}"
    "--cxx=${_target_cxx}")
  set(_xmake_build_args
    build -P . -F xmake.lua -y -vD)
  list(APPEND _xmake_config_args "--toolchain=llvm")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_config_args}
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE _cfg_err)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake configure failed for the textual consumer smoke check.\n"
    "stdout:\n${_cfg_out}\n"
    "stderr:\n${_cfg_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_mocks_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _mock_build_rc
  OUTPUT_VARIABLE _mock_build_out
  ERROR_VARIABLE _mock_build_err)
if(NOT _mock_build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_textual_mocks_xmake.\n"
    "stdout:\n${_mock_build_out}\n"
    "stderr:\n${_mock_build_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          ${_xmake_env}
          "${_xmake}" ${_xmake_build_args}
          gentest_consumer_textual_xmake
  WORKING_DIRECTORY "${_project_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_textual_xmake.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

set(_build_log "${_mock_build_out}\n${_mock_build_err}\n${_build_out}\n${_build_err}")
string(FIND "${_build_log}" "--host-clang" _host_clang_flag_pos)
string(FIND "${_build_log}" "${_clang_cxx}" _host_clang_path_pos)
if(_host_clang_flag_pos EQUAL -1 OR _host_clang_path_pos EQUAL -1)
  message(FATAL_ERROR
    "xmake textual consumer build did not forward the explicit host clang path to gentest_codegen.\n"
    "Expected host clang: ${_clang_cxx}\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()
foreach(_expected IN ITEMS
    "-DGENTEST_XMAKE_TEXTUAL_MOCKS_DEFINE=1"
    "-DGENTEST_XMAKE_TEXTUAL_MOCKS_CODEGEN=1"
    "-DGENTEST_XMAKE_TEXTUAL_CONSUMER_DEFINE=1"
    "-DGENTEST_XMAKE_TEXTUAL_CONSUMER_CODEGEN=1")
  string(FIND "${_build_log}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake textual consumer build did not pass through expected codegen flag '${_expected}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

set(_generated_glob_root "${_out_dir}/gen/*/*/*")
if(WIN32)
  set(_generated_glob_root "${_out_dir}/g/*")
endif()

foreach(_expected_glob IN ITEMS
    "${_generated_glob_root}/consumer_textual_mocks/gentest_consumer_mocks.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/tu_0000_consumer_textual_mocks_defs.gentest.h"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_registry.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_impl.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_registry__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_textual_mocks/consumer_textual_mocks_mock_impl__domain_0000_header.hpp"
    "${_generated_glob_root}/consumer_textual/tu_0000_cases.gentest.h")
  file(GLOB _expected_matches LIST_DIRECTORIES FALSE "${_expected_glob}")
  list(LENGTH _expected_matches _expected_match_count)
  if(NOT _expected_match_count EQUAL 1)
    message(FATAL_ERROR
      "xmake textual consumer build did not produce expected mock/codegen artifact '${_expected_glob}'.\n"
      "Matches:\n${_expected_matches}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

file(GLOB_RECURSE _consumer_bins
  LIST_DIRECTORIES FALSE
  "${_out_dir}/gentest_consumer_textual_xmake"
  "${_out_dir}/gentest_consumer_textual_xmake.exe")
list(LENGTH _consumer_bins _consumer_bin_count)
if(NOT _consumer_bin_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one built Xmake textual consumer binary, found ${_consumer_bin_count}.\n"
    "Candidates:\n${_consumer_bins}")
endif()
list(GET _consumer_bins 0 _consumer_bin)

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer listing failed.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_expected IN ITEMS
    "consumer/consumer/module_test"
    "consumer/consumer/module_mock"
    "consumer/consumer/module_bench"
    "consumer/consumer/module_jitter")
  string(FIND "${_list_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "The Xmake textual consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_test --kind=test
  RESULT_VARIABLE _module_test_rc
  OUTPUT_VARIABLE _module_test_out
  ERROR_VARIABLE _module_test_err)
if(NOT _module_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer plain test case failed.\n"
    "stdout:\n${_module_test_out}\n"
    "stderr:\n${_module_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_mock --kind=test
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer mock case failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_bench --kind=bench
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer bench failed.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_jitter --kind=jitter
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake textual consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
