if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBazelModuleConsumer.cmake: SOURCE_DIR not set")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/ModuleArtifactManifestAssertions.cmake")

set(_bazel "")
if(DEFINED BAZEL_EXECUTABLE AND NOT BAZEL_EXECUTABLE STREQUAL "")
  if(NOT EXISTS "${BAZEL_EXECUTABLE}")
    message(FATAL_ERROR "BAZEL_EXECUTABLE points to a missing bazel/bazelisk executable: ${BAZEL_EXECUTABLE}")
  endif()
  set(_bazel "${BAZEL_EXECUTABLE}")
elseif(NOT "$ENV{BAZEL_EXECUTABLE}" STREQUAL "")
  if(NOT EXISTS "$ENV{BAZEL_EXECUTABLE}")
    message(FATAL_ERROR "BAZEL_EXECUTABLE points to a missing bazel/bazelisk executable: $ENV{BAZEL_EXECUTABLE}")
  endif()
  set(_bazel "$ENV{BAZEL_EXECUTABLE}")
else()
  find_program(_bazel NAMES bazelisk bazel bazelisk.exe bazel.exe bazelisk.cmd bazel.cmd bazelisk.bat bazel.bat)
endif()
if(NOT _bazel)
  message(STATUS "bazel/bazelisk not found; skipping Bazel module consumer smoke check.")
  return()
endif()

set(_bazel_command "${_bazel}")
if(WIN32 AND _bazel MATCHES "\\.(cmd|bat)$")
  file(TO_NATIVE_PATH "${_bazel}" _bazel_native)
  set(_bazel_command cmd /d /c call "${_bazel_native}")
endif()
execute_process(
  COMMAND ${_bazel_command} --version
  RESULT_VARIABLE _bazel_version_rc
  OUTPUT_VARIABLE _bazel_version_out
  ERROR_VARIABLE _bazel_version_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(_bazel_version_rc EQUAL 0)
  set(_bazel_version_text "${_bazel_version_out}")
else()
  set(_bazel_version_text "version probe failed: ${_bazel_version_err}")
endif()

function(_gentest_resolve_bazel_binary_path base_path out_var)
  set(_candidates "${base_path}")
  if(WIN32)
    list(APPEND _candidates "${base_path}.exe" "${base_path}.cmd" "${base_path}.bat")
  endif()
  foreach(_candidate IN LISTS _candidates)
    if(EXISTS "${_candidate}")
      set(${out_var} "${_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_gentest_resolve_llvm_cmake_dirs clang_cxx out_llvm_dir out_clang_dir)
  get_filename_component(_clang_real "${clang_cxx}" REALPATH)
  get_filename_component(_clang_bin_dir "${_clang_real}" DIRECTORY)
  get_filename_component(_clang_prefix "${_clang_bin_dir}/.." ABSOLUTE)

  set(_llvm_candidates
    "${_clang_prefix}/lib/cmake/llvm"
    "${_clang_prefix}/lib64/cmake/llvm"
    /opt/homebrew/opt/llvm/lib/cmake/llvm
    /usr/local/opt/llvm/lib/cmake/llvm
    /usr/lib64/llvm22/lib64/cmake/llvm
    /usr/lib64/llvm21/lib64/cmake/llvm
    /usr/lib64/llvm20/lib64/cmake/llvm
    /usr/lib/llvm-22/lib/cmake/llvm
    /usr/lib/llvm-21/lib/cmake/llvm
    /usr/lib/llvm-20/lib/cmake/llvm)
  set(_clang_candidates
    "${_clang_prefix}/lib/cmake/clang"
    "${_clang_prefix}/lib64/cmake/clang"
    /opt/homebrew/opt/llvm/lib/cmake/clang
    /usr/local/opt/llvm/lib/cmake/clang
    /usr/lib64/llvm22/lib64/cmake/clang
    /usr/lib64/llvm21/lib64/cmake/clang
    /usr/lib64/llvm20/lib64/cmake/clang
    /usr/lib/llvm-22/lib/cmake/clang
    /usr/lib/llvm-21/lib/cmake/clang
    /usr/lib/llvm-20/lib/cmake/clang)

  set(_resolved_llvm "")
  if(DEFINED LLVM_DIR AND NOT LLVM_DIR STREQUAL "" AND EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    set(_resolved_llvm "${LLVM_DIR}")
  endif()
  if(_resolved_llvm STREQUAL "" AND NOT "$ENV{LLVM_DIR}" STREQUAL "" AND EXISTS "$ENV{LLVM_DIR}/LLVMConfig.cmake")
    set(_resolved_llvm "$ENV{LLVM_DIR}")
  endif()
  foreach(_candidate IN LISTS _llvm_candidates)
    if(_resolved_llvm STREQUAL "" AND EXISTS "${_candidate}/LLVMConfig.cmake")
      set(_resolved_llvm "${_candidate}")
      break()
    endif()
  endforeach()

  set(_resolved_clang "")
  if(DEFINED Clang_DIR AND NOT Clang_DIR STREQUAL "" AND EXISTS "${Clang_DIR}/ClangConfig.cmake")
    set(_resolved_clang "${Clang_DIR}")
  endif()
  if(_resolved_clang STREQUAL "" AND NOT "$ENV{Clang_DIR}" STREQUAL "" AND EXISTS "$ENV{Clang_DIR}/ClangConfig.cmake")
    set(_resolved_clang "$ENV{Clang_DIR}")
  endif()
  if(_resolved_clang STREQUAL "" AND NOT _resolved_llvm STREQUAL "")
    get_filename_component(_llvm_cmake_parent "${_resolved_llvm}" DIRECTORY)
    set(_clang_from_llvm "${_llvm_cmake_parent}/clang")
    if(EXISTS "${_clang_from_llvm}/ClangConfig.cmake")
      set(_resolved_clang "${_clang_from_llvm}")
    endif()
  endif()
  foreach(_candidate IN LISTS _clang_candidates)
    if(_resolved_clang STREQUAL "" AND EXISTS "${_candidate}/ClangConfig.cmake")
      set(_resolved_clang "${_candidate}")
      break()
    endif()
  endforeach()

  set(${out_llvm_dir} "${_resolved_llvm}" PARENT_SCOPE)
  set(${out_clang_dir} "${_resolved_clang}" PARENT_SCOPE)
endfunction()

set(_gentest_clang_search_paths
  $ENV{LLVM_BIN}
  /opt/homebrew/opt/llvm@21/bin
  /opt/homebrew/opt/llvm@20/bin
  /usr/local/opt/llvm@21/bin
  /usr/local/opt/llvm@20/bin
  /opt/homebrew/opt/llvm/bin
  /usr/local/opt/llvm/bin
  /opt/homebrew/bin
  /usr/local/bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin)

function(_gentest_filter_non_ccache_paths out_var)
  set(_filtered_paths "")
  foreach(_path IN LISTS ARGN)
    if("${_path}" STREQUAL "")
      continue()
    endif()
    if(_path MATCHES "(^|[/\\\\])ccache([/\\\\]|$)")
      continue()
    endif()
    list(APPEND _filtered_paths "${_path}")
  endforeach()
  list(REMOVE_DUPLICATES _filtered_paths)
  set(${out_var} "${_filtered_paths}" PARENT_SCOPE)
endfunction()

function(_gentest_resolve_non_ccache_clang candidate out_var)
  set(_resolved "${candidate}")
  if(NOT "${candidate}" STREQUAL "" AND EXISTS "${candidate}" AND candidate MATCHES "(^|[/\\\\])ccache([/\\\\]|$)")
    get_filename_component(_candidate_real "${candidate}" REALPATH)
    get_filename_component(_candidate_real_name "${_candidate_real}" NAME)
    if(EXISTS "${_candidate_real}" AND NOT _candidate_real_name MATCHES "^ccache(\\.exe)?$")
      set(_resolved "${_candidate_real}")
    else()
      set(_gentest_non_ccache_clang_search_paths "")
      if(NOT "$ENV{PATH}" STREQUAL "")
        cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST _gentest_env_path_entries NORMALIZE)
        _gentest_filter_non_ccache_paths(_gentest_env_non_ccache_clang_search_paths ${_gentest_env_path_entries})
        list(APPEND _gentest_non_ccache_clang_search_paths ${_gentest_env_non_ccache_clang_search_paths})
      endif()
      _gentest_filter_non_ccache_paths(_gentest_known_non_ccache_clang_search_paths ${_gentest_clang_search_paths})
      list(APPEND _gentest_non_ccache_clang_search_paths ${_gentest_known_non_ccache_clang_search_paths})
      list(REMOVE_DUPLICATES _gentest_non_ccache_clang_search_paths)
      set(_gentest_search_names "")
      foreach(_gentest_name IN LISTS ARGN)
        if(_gentest_name STREQUAL "clang++" OR _gentest_name STREQUAL "clang")
          list(APPEND _gentest_search_names "${_gentest_name}")
        endif()
      endforeach()
      list(APPEND _gentest_search_names ${ARGN})
      list(REMOVE_DUPLICATES _gentest_search_names)
      unset(_resolved_candidate CACHE)
      unset(_resolved_candidate)
      find_program(_resolved_candidate
        NAMES ${_gentest_search_names}
        PATHS ${_gentest_non_ccache_clang_search_paths}
        NO_DEFAULT_PATH
        NO_CACHE)
      if(_resolved_candidate)
        set(_resolved "${_resolved_candidate}")
      endif()
    endif()
  endif()
  set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

set(_resource_dir "")
set(_use_explicit_c_compiler FALSE)
set(_codegen_host_clang "")
if(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
  if(NOT EXISTS "${CXX_COMPILER}")
    message(FATAL_ERROR
      "CXX_COMPILER points to a missing clang++ executable for the Bazel module consumer smoke check.\n"
      "CXX_COMPILER: ${CXX_COMPILER}")
  endif()
  _gentest_resolve_non_ccache_clang("${CXX_COMPILER}" _configured_cxx clang++-22 clang++-21 clang++-20 clang++)
  execute_process(
    COMMAND "${_configured_cxx}" -print-resource-dir
    RESULT_VARIABLE _configured_resource_rc
    OUTPUT_VARIABLE _configured_resource_out
    ERROR_VARIABLE _configured_resource_err
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_configured_resource_rc EQUAL 0 AND NOT _configured_resource_out STREQUAL "")
    set(_codegen_host_clang "${_configured_cxx}")
    set(_resource_dir "${_configured_resource_out}")
    set(_use_explicit_c_compiler TRUE)
  endif()
endif()
if(_codegen_host_clang STREQUAL "" AND NOT "$ENV{GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
  set(_codegen_host_clang "$ENV{GENTEST_CODEGEN_HOST_CLANG}")
  if(NOT EXISTS "${_codegen_host_clang}")
    message(FATAL_ERROR
      "GENTEST_CODEGEN_HOST_CLANG points to a missing clang++ executable for the Bazel module consumer smoke check.\n"
      "GENTEST_CODEGEN_HOST_CLANG: ${_codegen_host_clang}")
  endif()
  _gentest_resolve_non_ccache_clang("${_codegen_host_clang}" _codegen_host_clang clang++-22 clang++-21 clang++-20 clang++)
endif()
if(_codegen_host_clang STREQUAL "")
  if(NOT "$ENV{LLVM_BIN}" STREQUAL "" AND EXISTS "$ENV{LLVM_BIN}/clang++")
    set(_codegen_host_clang "$ENV{LLVM_BIN}/clang++")
  endif()
endif()
if(_codegen_host_clang STREQUAL "" OR NOT EXISTS "${_codegen_host_clang}")
  find_program(_codegen_host_clang NAMES clang++-22 clang++-21 clang++-20 clang++
    PATHS ${_gentest_clang_search_paths}
    NO_DEFAULT_PATH)
endif()
if(NOT _codegen_host_clang)
  find_program(_codegen_host_clang NAMES clang++-22 clang++-21 clang++-20 clang++)
  if(NOT _codegen_host_clang)
    message(STATUS "clang++ not found; skipping Bazel module consumer smoke check.")
    return()
  endif()
endif()
_gentest_resolve_non_ccache_clang("${_codegen_host_clang}" _codegen_host_clang clang++-22 clang++-21 clang++-20 clang++)
set(_clang_cxx "${_codegen_host_clang}")
get_filename_component(_clang_bin_dir "${_clang_cxx}" DIRECTORY)

if(_use_explicit_c_compiler AND DEFINED C_COMPILER AND NOT C_COMPILER STREQUAL "")
  if(NOT EXISTS "${C_COMPILER}")
    message(FATAL_ERROR
      "C_COMPILER points to a missing clang executable for the Bazel module consumer smoke check.\n"
      "C_COMPILER: ${C_COMPILER}")
  endif()
  _gentest_resolve_non_ccache_clang("${C_COMPILER}" _clang_cc clang-22 clang-21 clang-20 clang)
else()
  find_program(_clang_cc NAMES clang-22 clang-21 clang-20 clang
    PATHS "${_clang_bin_dir}" ${_gentest_clang_search_paths}
    NO_DEFAULT_PATH)
  if(NOT _clang_cc)
    find_program(_clang_cc NAMES clang-22 clang-21 clang-20 clang
      PATHS ${_gentest_clang_search_paths})
  endif()
endif()
if(NOT _clang_cc)
  message(FATAL_ERROR
    "Failed to locate a clang executable adjacent to the resolved host clang for the Bazel module consumer smoke check.\n"
    "GENTEST_CODEGEN_HOST_CLANG: ${_clang_cxx}\n"
    "clang bin dir: ${_clang_bin_dir}")
endif()
_gentest_resolve_non_ccache_clang("${_clang_cc}" _clang_cc clang-22 clang-21 clang-20 clang)

if(_resource_dir STREQUAL "")
  execute_process(
    COMMAND "${_clang_cxx}" -print-resource-dir
    RESULT_VARIABLE _resource_rc
    OUTPUT_VARIABLE _resource_out
    ERROR_VARIABLE _resource_err
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _resource_rc EQUAL 0 OR _resource_out STREQUAL "")
    message(FATAL_ERROR
      "Failed to query clang resource dir for the Bazel module consumer smoke check.\n"
      "stdout:\n${_resource_out}\n"
      "stderr:\n${_resource_err}")
  endif()
  set(_resource_dir "${_resource_out}")
endif()
_gentest_resolve_llvm_cmake_dirs("${_clang_cxx}" _llvm_dir _clang_dir)
if(_llvm_dir STREQUAL "" OR _clang_dir STREQUAL "")
  message(FATAL_ERROR
    "Failed to locate supported LLVM/Clang CMake package directories for the Bazel module consumer smoke check.\n"
    "host clang: ${_clang_cxx}\n"
    "LLVM_DIR: ${_llvm_dir}\n"
    "Clang_DIR: ${_clang_dir}")
endif()
set(_path_sep ":")
if(WIN32)
  set(_path_sep ";")
endif()
set(_tool_path "${_clang_bin_dir}${_path_sep}$ENV{PATH}")
get_filename_component(_source_parent "${SOURCE_DIR}" DIRECTORY)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _bazel_run_id)
set(_bazel_smoke_root "${_source_parent}/.bazel-smoke-module-${_bazel_run_id}")
set(_bazel_output_root "${_bazel_smoke_root}/output")
set(_bazel_repo_contents_cache "${_bazel_smoke_root}/repo-cache")
file(MAKE_DIRECTORY "${_bazel_output_root}")
file(MAKE_DIRECTORY "${_bazel_repo_contents_cache}")

set(_gentest_bazel_build_args
  --output_user_root=${_bazel_output_root}
  build
  --experimental_cpp_modules
  --repo_contents_cache=${_bazel_repo_contents_cache}
  --action_env=CCACHE_DISABLE
  --action_env=PATH
  --host_action_env=CCACHE_DISABLE
  --host_action_env=PATH
  --action_env=CC
  --action_env=CXX
  --action_env=LLVM_BIN
  --action_env=LLVM_DIR
  --action_env=Clang_DIR
  --action_env=GENTEST_CODEGEN_HOST_CLANG
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR
  --host_action_env=CC
  --host_action_env=CXX
  --host_action_env=LLVM_BIN
  --host_action_env=LLVM_DIR
  --host_action_env=Clang_DIR
  --host_action_env=GENTEST_CODEGEN_HOST_CLANG
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR
  --host_action_env=HOME
  --repo_env=PATH
  --repo_env=CC
  --repo_env=CXX
  --repo_env=LLVM_BIN
  --repo_env=LLVM_DIR
  --repo_env=Clang_DIR
  --repo_env=GENTEST_CODEGEN_HOST_CLANG
  --repo_env=GENTEST_CODEGEN_RESOURCE_DIR
  --repo_env=HOME
  --action_env=HOME
  --verbose_failures
  --sandbox_debug
  //:gentest_consumer_module_mocks
  //:gentest_consumer_module_bazel)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "CCACHE_DISABLE=1"
          "PATH=${_tool_path}"
          "CC=${_clang_cc}"
          "CXX=${_clang_cxx}"
          "LLVM_BIN=${_clang_bin_dir}"
          "LLVM_DIR=${_llvm_dir}"
          "Clang_DIR=${_clang_dir}"
          "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
          "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}"
          "HOME=$ENV{HOME}"
          ${_bazel_command}
          ${_gentest_bazel_build_args}
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Bazel build failed for gentest_consumer_module_bazel.\n"
    "bazel: ${_bazel}\n"
    "bazel version: ${_bazel_version_text}\n"
    "bootstrap clang: ${_clang_cc}\n"
    "host clang: ${_clang_cxx}\n"
    "LLVM_DIR: ${_llvm_dir}\n"
    "Clang_DIR: ${_clang_dir}\n"
    "resource dir: ${_resource_dir}\n"
    "output root: ${_bazel_output_root}\n"
    "repo cache: ${_bazel_repo_contents_cache}\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "CCACHE_DISABLE=1"
          "PATH=${_tool_path}"
          "CC=${_clang_cc}"
          "CXX=${_clang_cxx}"
          "LLVM_BIN=${_clang_bin_dir}"
          "LLVM_DIR=${_llvm_dir}"
          "Clang_DIR=${_clang_dir}"
          "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
          "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}"
          "HOME=$ENV{HOME}"
          ${_bazel_command}
          --output_user_root=${_bazel_output_root}
          info
          bazel-bin
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _bazel_bin_rc
  OUTPUT_VARIABLE _bazel_bin_out
  ERROR_VARIABLE _bazel_bin_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _bazel_bin_rc EQUAL 0 OR _bazel_bin_out STREQUAL "")
  message(FATAL_ERROR
    "Failed to resolve bazel-bin for the Bazel module consumer smoke check.\n"
    "bazel: ${_bazel}\n"
    "bazel version: ${_bazel_version_text}\n"
    "output root: ${_bazel_output_root}\n"
    "stdout:\n${_bazel_bin_out}\n"
    "stderr:\n${_bazel_bin_err}")
endif()
file(TO_CMAKE_PATH "${_bazel_bin_out}" _bazel_bin_dir)
set(_consumer_binary_base "${_bazel_bin_dir}/gentest_consumer_module_bazel")
_gentest_resolve_bazel_binary_path("${_consumer_binary_base}" _consumer_binary)
if(_consumer_binary STREQUAL "")
  message(FATAL_ERROR "Expected Bazel module consumer binary not found: ${_consumer_binary_base}")
endif()

foreach(_expected_file IN ITEMS
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest/consumer_mocks.cppm"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0000_header.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0000_header.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0001_gentest_consumer_service.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0001_gentest_consumer_service.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0002_gentest_consumer_mock_defs.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0002_gentest_consumer_mock_defs.hpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.module.gentest.cppm"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.gentest.h"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.module.gentest.cppm"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.gentest.h"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_bazel/suite_0000.cppm"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_bazel/tu_0000_suite_0000.registration.gentest.cpp"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_bazel/tu_0000_suite_0000.gentest.h"
    "${_bazel_bin_dir}/gen/gentest_consumer_module_bazel/gentest_consumer_module_bazel.artifact_manifest.json")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR
      "Bazel module mock target build did not produce expected mockgen artifact '${_expected_file}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

gentest_expect_module_artifact_manifest(
  "${_bazel_bin_dir}/gen/gentest_consumer_module_bazel/gentest_consumer_module_bazel.artifact_manifest.json"
  "gentest.consumer_cases"
  "gentest_consumer_module_bazel:"
  "suite_0000.cppm"
  "gentest_consumer_module_bazel/tu_0000_suite_0000.registration.gentest.cpp")

execute_process(
  COMMAND "${_consumer_binary}" --list
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer listing failed.\n"
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
      "The Bazel module consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_binary}" --run=consumer/consumer/module_test --kind=test
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _plain_test_rc
  OUTPUT_VARIABLE _plain_test_out
  ERROR_VARIABLE _plain_test_err)
if(NOT _plain_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer test case failed.\n"
    "stdout:\n${_plain_test_out}\n"
    "stderr:\n${_plain_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_binary}" --run=consumer/consumer/module_mock --kind=test
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer mock case failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_binary}" --run=consumer/consumer/module_bench --kind=bench
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer bench failed.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${_consumer_binary}" --run=consumer/consumer/module_jitter --kind=jitter
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
