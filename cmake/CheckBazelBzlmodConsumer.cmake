if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBazelBzlmodConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckBazelBzlmodConsumer.cmake: BUILD_ROOT not set")
endif()

find_program(_bazel NAMES bazelisk bazel bazelisk.exe bazel.exe bazelisk.cmd bazel.cmd bazelisk.bat bazel.bat)
if(NOT _bazel)
  message(STATUS "bazel/bazelisk not found; skipping Bazel Bzlmod consumer check.")
  return()
endif()

set(_bazel_command "${_bazel}")
if(WIN32 AND _bazel MATCHES "\\.(cmd|bat)$")
  file(TO_NATIVE_PATH "${_bazel}" _bazel_native)
  set(_bazel_command cmd /d /c call "${_bazel_native}")
endif()

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
  if(NOT "$ENV{LLVM_DIR}" STREQUAL "" AND EXISTS "$ENV{LLVM_DIR}/LLVMConfig.cmake")
    set(_resolved_llvm "$ENV{LLVM_DIR}")
  endif()
  foreach(_candidate IN LISTS _llvm_candidates)
    if(_resolved_llvm STREQUAL "" AND EXISTS "${_candidate}/LLVMConfig.cmake")
      set(_resolved_llvm "${_candidate}")
      break()
    endif()
  endforeach()

  set(_resolved_clang "")
  if(NOT "$ENV{Clang_DIR}" STREQUAL "" AND EXISTS "$ENV{Clang_DIR}/ClangConfig.cmake")
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

set(_codegen_host_clang "$ENV{GENTEST_CODEGEN_HOST_CLANG}")
if(NOT _codegen_host_clang STREQUAL "" AND NOT EXISTS "${_codegen_host_clang}")
  message(FATAL_ERROR "GENTEST_CODEGEN_HOST_CLANG points to a missing clang++ executable: ${_codegen_host_clang}")
endif()
if(_codegen_host_clang STREQUAL "")
  find_program(_codegen_host_clang NAMES clang++-22 clang++-21 clang++-20 clang++
    PATHS ${_gentest_clang_search_paths}
    NO_DEFAULT_PATH)
endif()
if(NOT _codegen_host_clang)
  find_program(_codegen_host_clang NAMES clang++-22 clang++-21 clang++-20 clang++)
endif()
if(NOT _codegen_host_clang)
  message(STATUS "clang++ not found; skipping Bazel Bzlmod consumer check.")
  return()
endif()

get_filename_component(_clang_bin_dir "${_codegen_host_clang}" DIRECTORY)
find_program(_clang_cc NAMES clang-22 clang-21 clang-20 clang
  PATHS "${_clang_bin_dir}" ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang-22 clang-21 clang-20 clang
    PATHS ${_gentest_clang_search_paths})
endif()
if(NOT _clang_cc)
  message(FATAL_ERROR "Failed to locate clang adjacent to ${_codegen_host_clang}")
endif()

execute_process(
  COMMAND "${_codegen_host_clang}" -print-resource-dir
  RESULT_VARIABLE _resource_rc
  OUTPUT_VARIABLE _resource_out
  ERROR_VARIABLE _resource_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _resource_rc EQUAL 0 OR _resource_out STREQUAL "")
  message(FATAL_ERROR "Failed to query clang resource dir.\nstdout:\n${_resource_out}\nstderr:\n${_resource_err}")
endif()
set(_resource_dir "${_resource_out}")

_gentest_resolve_llvm_cmake_dirs("${_codegen_host_clang}" _llvm_dir _clang_dir)
if(_llvm_dir STREQUAL "" OR _clang_dir STREQUAL "")
  message(FATAL_ERROR
    "Failed to locate LLVM/Clang CMake package directories for the Bazel Bzlmod consumer check.\n"
    "host clang: ${_codegen_host_clang}\nLLVM_DIR: ${_llvm_dir}\nClang_DIR: ${_clang_dir}")
endif()

set(_path_sep ":")
if(WIN32)
  set(_path_sep ";")
endif()
set(_tool_path "${_clang_bin_dir}${_path_sep}$ENV{PATH}")

set(_fixture_dir "${BUILD_ROOT}/bazel_bzlmod_consumer")
set(_workspace_dir "${_fixture_dir}/workspace")
set(_output_root "${_fixture_dir}/output")
set(_repo_cache "${_fixture_dir}/repo-cache")
file(REMOVE_RECURSE "${_fixture_dir}")
file(MAKE_DIRECTORY "${_workspace_dir}")
file(COPY "${SOURCE_DIR}/tests/downstream/bazel_bzlmod_consumer/" DESTINATION "${_workspace_dir}")
set(GENTEST_SOURCE_DIR "${SOURCE_DIR}")
configure_file(
  "${SOURCE_DIR}/tests/downstream/bazel_bzlmod_consumer/MODULE.bazel.in"
  "${_workspace_dir}/MODULE.bazel"
  @ONLY)
file(MAKE_DIRECTORY "${_output_root}")
file(MAKE_DIRECTORY "${_repo_cache}")

set(_bazel_env
  "CCACHE_DISABLE=1"
  "PATH=${_tool_path}"
  "CC=${_clang_cc}"
  "CXX=${_codegen_host_clang}"
  "LLVM_BIN=${_clang_bin_dir}"
  "LLVM_DIR=${_llvm_dir}"
  "Clang_DIR=${_clang_dir}"
  "GENTEST_CODEGEN_HOST_CLANG=${_codegen_host_clang}"
  "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}"
  "HOME=$ENV{HOME}")

set(_build_args
  --output_user_root=${_output_root}
  build
  --repo_contents_cache=${_repo_cache}
  --experimental_cpp_modules
  --action_env=CCACHE_DISABLE
  --action_env=PATH
  --action_env=CC
  --action_env=CXX
  --action_env=LLVM_BIN
  --action_env=LLVM_DIR
  --action_env=Clang_DIR
  --action_env=GENTEST_CODEGEN_HOST_CLANG
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR
  --host_action_env=CCACHE_DISABLE
  --host_action_env=PATH
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
  //:gentest_downstream_textual_mocks
  //:gentest_downstream_textual
  //:gentest_downstream_module_mocks
  //:gentest_downstream_module)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          ${_bazel_command}
          ${_build_args}
  WORKING_DIRECTORY "${_workspace_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Bazel Bzlmod downstream build failed.\n"
    "workspace: ${_workspace_dir}\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          ${_bazel_command}
          --output_user_root=${_output_root}
          info
          bazel-bin
  WORKING_DIRECTORY "${_workspace_dir}"
  RESULT_VARIABLE _bazel_bin_rc
  OUTPUT_VARIABLE _bazel_bin_out
  ERROR_VARIABLE _bazel_bin_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _bazel_bin_rc EQUAL 0 OR _bazel_bin_out STREQUAL "")
  message(FATAL_ERROR
    "Failed to resolve bazel-bin for the Bazel Bzlmod consumer check.\n"
    "stdout:\n${_bazel_bin_out}\n"
    "stderr:\n${_bazel_bin_err}")
endif()
file(TO_CMAKE_PATH "${_bazel_bin_out}" _bazel_bin_dir)

foreach(_expected_file IN ITEMS
    "${_bazel_bin_dir}/gen/gentest_downstream_textual_mocks/gentest_downstream_mocks.hpp"
    "${_bazel_bin_dir}/gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_registry.hpp"
    "${_bazel_bin_dir}/gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_impl.hpp"
    "${_bazel_bin_dir}/gen/gentest_downstream_module_mocks/downstream/bazel/consumer_mocks.cppm"
    "${_bazel_bin_dir}/gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_registry.hpp"
    "${_bazel_bin_dir}/gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_impl.hpp"
    "${_bazel_bin_dir}/gen/gentest_downstream_module/tu_0000_suite_0000.gentest.h")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR
      "Bazel Bzlmod consumer build did not produce expected artifact '${_expected_file}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

foreach(_binary IN ITEMS
    "${_bazel_bin_dir}/gentest_downstream_textual"
    "${_bazel_bin_dir}/gentest_downstream_module")
  if(NOT EXISTS "${_binary}")
    message(FATAL_ERROR "Expected Bazel downstream binary not found: ${_binary}")
  endif()

  execute_process(
    COMMAND "${_binary}" --list
    WORKING_DIRECTORY "${_workspace_dir}"
    RESULT_VARIABLE _list_rc
    OUTPUT_VARIABLE _list_out
    ERROR_VARIABLE _list_err)
  if(NOT _list_rc EQUAL 0)
    message(FATAL_ERROR
      "Running ${_binary} --list failed.\n"
      "stdout:\n${_list_out}\n"
      "stderr:\n${_list_err}")
  endif()

  foreach(_expected IN ITEMS
      "downstream/bazel/test"
      "downstream/bazel/mock"
      "downstream/bazel/bench"
      "downstream/bazel/jitter")
    string(FIND "${_list_out}" "${_expected}" _expected_pos)
    if(_expected_pos EQUAL -1)
      message(FATAL_ERROR
        "The downstream Bazel listing for ${_binary} is missing '${_expected}'.\n"
        "stdout:\n${_list_out}")
    endif()
  endforeach()

  foreach(_kind IN ITEMS "test" "bench" "jitter")
    if(_kind STREQUAL "test")
      set(_case_names "downstream/bazel/test;downstream/bazel/mock")
    elseif(_kind STREQUAL "bench")
      set(_case_names "downstream/bazel/bench")
    else()
      set(_case_names "downstream/bazel/jitter")
    endif()
    foreach(_case_name IN LISTS _case_names)
      execute_process(
        COMMAND "${_binary}" "--run=${_case_name}" "--kind=${_kind}"
        WORKING_DIRECTORY "${_workspace_dir}"
        RESULT_VARIABLE _case_rc
        OUTPUT_VARIABLE _case_out
        ERROR_VARIABLE _case_err)
      if(NOT _case_rc EQUAL 0)
        message(FATAL_ERROR
          "Running ${_binary} ${_case_name} (${_kind}) failed.\n"
          "stdout:\n${_case_out}\n"
          "stderr:\n${_case_err}")
      endif()
    endforeach()
  endforeach()
endforeach()
