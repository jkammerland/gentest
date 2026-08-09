# Validates the portable Gentest codegen action contract with a staged,
# label-addressed exec toolchain. This is deliberately a correctness/cache
# check: it never interprets wall-clock duration as evidence.

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CheckBazelCodegenActionCache.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR BUILD_ROOT STREQUAL "")
  message(FATAL_ERROR "CheckBazelCodegenActionCache.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROG OR PROG STREQUAL "" OR NOT EXISTS "${PROG}")
  message(FATAL_ERROR "CheckBazelCodegenActionCache.cmake requires a built gentest_codegen via -DPROG=<path>")
endif()
if(WIN32)
  message(STATUS "GENTEST_SKIP_TEST: Bazel codegen disk-cache fixture currently stages Unix executable labels only.")
  return()
endif()

if(DEFINED BAZEL_EXECUTABLE AND NOT BAZEL_EXECUTABLE STREQUAL "")
  set(_bazel "${BAZEL_EXECUTABLE}")
else()
  find_program(_bazel NAMES bazelisk bazel)
endif()
if(NOT _bazel)
  message(STATUS "GENTEST_SKIP_TEST: bazel/bazelisk not found")
  return()
endif()

function(_gentest_usable_clang candidate out_var)
  if(NOT EXISTS "${candidate}")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()
  get_filename_component(_candidate_real "${candidate}" REALPATH)
  get_filename_component(_candidate_name "${_candidate_real}" NAME)
  if(_candidate_name MATCHES "^ccache(\\.exe)?$")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND "${candidate}" --version
    RESULT_VARIABLE _version_rc
    OUTPUT_VARIABLE _version_out
    ERROR_VARIABLE _version_err)
  string(TOLOWER "${_version_out}${_version_err}" _version_text)
  if(_version_rc EQUAL 0 AND _version_text MATCHES "clang")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

set(_clang "")
foreach(_candidate IN ITEMS "${CXX_COMPILER}" "$ENV{CXX}")
  if(NOT _clang STREQUAL "")
    continue()
  endif()
  _gentest_usable_clang("${_candidate}" _candidate_is_clang)
  if(_candidate_is_clang)
    set(_clang "${_candidate}")
  endif()
endforeach()
if(_clang STREQUAL "")
  foreach(_candidate IN ITEMS
      /usr/bin/clang++-23 /usr/bin/clang++-22 /usr/bin/clang++-21 /usr/bin/clang++-20 /usr/bin/clang++
      /usr/local/bin/clang++-23 /usr/local/bin/clang++-22 /usr/local/bin/clang++-21 /usr/local/bin/clang++-20 /usr/local/bin/clang++
      /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++)
    if(NOT _clang STREQUAL "")
      continue()
    endif()
    _gentest_usable_clang("${_candidate}" _candidate_is_clang)
    if(_candidate_is_clang)
      set(_clang "${_candidate}")
    endif()
  endforeach()
endif()
if(_clang STREQUAL "")
  find_program(_found_clang NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++)
  _gentest_usable_clang("${_found_clang}" _found_is_clang)
  if(_found_is_clang)
    set(_clang "${_found_clang}")
  endif()
endif()
if(_clang STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: no non-ccache clang++ available")
  return()
endif()

get_filename_component(_clang_bin_dir "${_clang}" DIRECTORY)
set(_clang_c "${_clang_bin_dir}/clang")
if(NOT EXISTS "${_clang_c}")
  find_program(_clang_c NAMES clang-23 clang-22 clang-21 clang-20 clang PATHS "${_clang_bin_dir}")
endif()
if(NOT _clang_c OR NOT EXISTS "${_clang_c}")
  message(STATUS "GENTEST_SKIP_TEST: clang adjacent to ${_clang} not found")
  return()
endif()
execute_process(
  COMMAND "${_clang}" -print-resource-dir
  RESULT_VARIABLE _resource_dir_rc
  OUTPUT_VARIABLE _clang_resource_dir
  ERROR_VARIABLE _resource_dir_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _resource_dir_rc EQUAL 0 OR NOT IS_DIRECTORY "${_clang_resource_dir}")
  message(STATUS "GENTEST_SKIP_TEST: clang resource directory is unavailable for ${_clang}: ${_resource_dir_err}")
  return()
endif()
get_filename_component(_clang_resource_version "${_clang_resource_dir}" NAME)

get_filename_component(_source_parent "${SOURCE_DIR}" DIRECTORY)
string(MD5 _cache_fixture_hash "${BUILD_ROOT}|${SOURCE_DIR}")
string(SUBSTRING "${_cache_fixture_hash}" 0 12 _cache_fixture_suffix)
set(_root "${_source_parent}/.bazel-codegen-action-cache-${_cache_fixture_suffix}")
set(_tool_repo "${_root}/gentest_local_exec_tools")
set(_disk_cache "${_root}/disk-cache")
set(_repo_contents_cache "${_root}/repo-cache")
set(_output_one "${_root}/output-one")
set(_output_two "${_root}/output-two")
set(_output_three "${_root}/output-three")
set(_output_migration "${_root}/output-migration")
set(_output_local "${_root}/output-local")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_tool_repo}" "${_disk_cache}" "${_repo_contents_cache}")

# The staged tools model the label shape of a prebuilt distribution: every
# executable is a label, the generator is not built by a shell/genrule action,
# and the copied Clang resource directory is both declared and used by the
# staged compiler. The generator's system shared-library closure is deliberately
# not copied here, so this is intentionally not a complete remotely
# executable bundle; production documentation requires one.
file(COPY_FILE "${PROG}" "${_tool_repo}/gentest_codegen")
file(CHMOD "${_tool_repo}/gentest_codegen"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(MAKE_DIRECTORY "${_tool_repo}/bin" "${_tool_repo}/lib/clang")
file(COPY_FILE "${_clang}" "${_tool_repo}/bin/clang++")
file(CHMOD "${_tool_repo}/bin/clang++"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(COPY "${_clang_resource_dir}/" DESTINATION "${_tool_repo}/lib/clang/${_clang_resource_version}")
file(MAKE_DIRECTORY "${_tool_repo}/MacOSX.sdk/usr/include")
file(WRITE "${_tool_repo}/MacOSX.sdk/SDKSettings.json" "{}\n")
file(WRITE "${_tool_repo}/MacOSX.sdk/usr/include/gentest_sdk_sentinel.h" "#pragma once\n")
execute_process(
  COMMAND "${_tool_repo}/bin/clang++" -print-resource-dir
  RESULT_VARIABLE _staged_resource_dir_rc
  OUTPUT_VARIABLE _staged_resource_dir
  ERROR_VARIABLE _staged_resource_dir_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(_expected_staged_resource_dir
  "${_tool_repo}/lib/clang/${_clang_resource_version}" REALPATH)
get_filename_component(_staged_resource_dir_real "${_staged_resource_dir}" REALPATH)
if(NOT _staged_resource_dir_rc EQUAL 0 OR
   NOT _staged_resource_dir_real STREQUAL "${_expected_staged_resource_dir}")
  message(FATAL_ERROR
    "Staged clang did not resolve its declared resource directory.\n"
    "expected: ${_expected_staged_resource_dir}\n"
    "actual: ${_staged_resource_dir}\n${_staged_resource_dir_err}")
endif()
file(WRITE "${_tool_repo}/MODULE.bazel" "module(name = \"gentest_local_exec_tools\")\n")
file(WRITE "${_tool_repo}/BUILD.bazel" [=[
load(
    "@gentest//bazel:defs.bzl",
    "gentest_attach_codegen_textual",
    "gentest_codegen_toolchain",
)

filegroup(
    name = "clang_runtime_files",
    srcs = glob(["lib/clang/**"]),
)

filegroup(
    name = "macos_sdk_files",
    srcs = glob(["MacOSX.sdk/**"]),
)

gentest_codegen_toolchain(
    name = "impl",
    codegen = ":gentest_codegen",
    clang = ":bin/clang++",
    runtime_files = [":clang_runtime_files", ":macos_sdk_files"],
    macos_sdk_root = "MacOSX.sdk/SDKSettings.json",
)

toolchain(
    name = "gentest_exec_toolchain",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
)

# Keep the legacy macro parameter parse-compatible, but it must fail at
# analysis rather than reintroducing an undeclared absolute host compiler.
gentest_attach_codegen_textual(
    name = "legacy_host_clang",
    src = "legacy_cases.cpp",
    codegen_host_clang = "/legacy/clang++",
)
]=])
file(WRITE "${_tool_repo}/legacy_cases.cpp" "// Analysis must reject codegen_host_clang before this source is parsed.\n")

set(_common_args
  --disk_cache=${_disk_cache}
  --repo_contents_cache=${_repo_contents_cache}
  --override_repository=gentest_local_exec_tools=${_tool_repo}
  --experimental_cpp_modules
  //:gentest_consumer_textual_bazel__codegen
  //:gentest_consumer_module_bazel__codegen)
set(_bazel_env
  "CCACHE_DISABLE=1"
  "CC=${_clang_c}"
  "CXX=${_clang}"
  "HOME=$ENV{HOME}")
set(_local_bazel_env
  ${_bazel_env}
  "GENTEST_BAZEL_LOCAL_CLANG=${_clang}")

# The source-package fallback remains usable for local development, but its
# tool closure is deliberately not a remote bundle. Its actions must carry
# no-remote/no-cache; the staged packaged-label toolchain below must not.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_local_bazel_env}
          "${_bazel}"
          --output_user_root=${_output_local}
          --max_idle_secs=5
          aquery
          --output=text
          "mnemonic(\"GentestTextualSuiteCodegen\", deps(//:gentest_consumer_textual_bazel__codegen))"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _local_aquery_rc
  OUTPUT_VARIABLE _local_aquery_out
  ERROR_VARIABLE _local_aquery_err)
if(NOT _local_aquery_rc EQUAL 0)
  message(FATAL_ERROR
    "Bazel aquery for the local fallback failed.\nstdout:\n${_local_aquery_out}\nstderr:\n${_local_aquery_err}")
endif()
string(FIND "${_local_aquery_out}" "no-remote" _local_no_remote_pos)
string(FIND "${_local_aquery_out}" "no-cache" _local_no_cache_pos)
if(_local_no_remote_pos EQUAL -1 OR _local_no_cache_pos EQUAL -1)
  message(FATAL_ERROR
    "Local fallback codegen action does not disable execution/cache reuse.\n${_local_aquery_out}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          "${_bazel}"
          --output_user_root=${_output_one}
          --max_idle_secs=5
          aquery
          --output=text
          --repo_contents_cache=${_repo_contents_cache}
          --override_repository=gentest_local_exec_tools=${_tool_repo}
          "mnemonic(\"GentestTextualSuiteCodegen\", deps(//:gentest_consumer_textual_bazel__codegen))"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _aquery_rc
  OUTPUT_VARIABLE _aquery_out
  ERROR_VARIABLE _aquery_err)
if(NOT _aquery_rc EQUAL 0)
  message(FATAL_ERROR "Bazel aquery for staged exec tools failed.\nstdout:\n${_aquery_out}\nstderr:\n${_aquery_err}")
endif()
foreach(_required IN ITEMS
    "--source-root"
    "--host-clang"
    "gentest_codegen"
    "bin/clang++"
    "lib/clang/${_clang_resource_version}/include/stddef.h"
    "MacOSX.sdk/usr/include/gentest_sdk_sentinel.h"
    "SDKROOT"
    "tests/consumer/cases.cpp"
    "tests/consumer/bazel_private_case_value.hpp"
    "tests/consumer/bazel_dep_case_value.hpp"
    "tests/consumer/bazel_mock_dep/mock_dep.hpp"
    "GENTEST_BAZEL_MOCK_DEP=1"
    "include/gentest/runner.h"
    "gentest_consumer_mocks.hpp")
  string(FIND "${_aquery_out}" "${_required}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR "Bazel codegen aquery is missing declared exec-tool contract token '${_required}'.\n${_aquery_out}")
  endif()
endforeach()
string(FIND "${_aquery_out}" "no-remote" _staged_no_remote_pos)
string(FIND "${_aquery_out}" "no-cache" _staged_no_cache_pos)
if(NOT _staged_no_remote_pos EQUAL -1 OR NOT _staged_no_cache_pos EQUAL -1)
  message(FATAL_ERROR "Packaged-label codegen action unexpectedly disables remote execution/cache.\n${_aquery_out}")
endif()
string(FIND "${_aquery_out}" "${SOURCE_DIR}" _absolute_source_pos)
if(NOT _absolute_source_pos EQUAL -1)
  message(FATAL_ERROR "Bazel codegen aquery leaked an absolute source checkout path.\n${_aquery_out}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          "${_bazel}"
          --output_user_root=${_output_migration}
          --max_idle_secs=5
          build
          --repo_contents_cache=${_repo_contents_cache}
          --override_repository=gentest_local_exec_tools=${_tool_repo}
          @gentest_local_exec_tools//:legacy_host_clang__codegen
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _legacy_host_clang_rc
  OUTPUT_VARIABLE _legacy_host_clang_out
  ERROR_VARIABLE _legacy_host_clang_err)
if(_legacy_host_clang_rc EQUAL 0)
  message(FATAL_ERROR
    "Nonempty codegen_host_clang unexpectedly analyzed successfully.\n"
    "stdout:\n${_legacy_host_clang_out}\nstderr:\n${_legacy_host_clang_err}")
endif()
set(_legacy_host_clang_log "${_legacy_host_clang_out}\n${_legacy_host_clang_err}")
foreach(_migration_token IN ITEMS
    "codegen_host_clang no longer accepts an absolute host path"
    "Register a gentest_codegen_toolchain")
  string(FIND "${_legacy_host_clang_log}" "${_migration_token}" _migration_token_pos)
  if(_migration_token_pos EQUAL -1)
    message(FATAL_ERROR
      "Legacy codegen_host_clang failure did not provide migration guidance '${_migration_token}'.\n"
      "${_legacy_host_clang_log}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          "${_bazel}"
          --output_user_root=${_output_one}
          --max_idle_secs=5
          aquery
          --experimental_cpp_modules
          --output=text
          --repo_contents_cache=${_repo_contents_cache}
          --override_repository=gentest_local_exec_tools=${_tool_repo}
          "mnemonic(\"GentestModule.*Codegen\", deps(//:gentest_consumer_module_bazel__codegen))"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _module_aquery_rc
  OUTPUT_VARIABLE _module_aquery_out
  ERROR_VARIABLE _module_aquery_err)
if(NOT _module_aquery_rc EQUAL 0)
  message(FATAL_ERROR
    "Bazel module codegen aquery for staged exec tools failed.\nstdout:\n${_module_aquery_out}\nstderr:\n${_module_aquery_err}")
endif()
foreach(_required_module_action_token IN ITEMS
    "GentestModuleMocksCodegen"
    "GentestModuleSuiteCodegen"
    "--host-clang"
    "--scan-deps-mode=OFF"
    "gentest_codegen"
    "bin/clang++"
    "lib/clang/${_clang_resource_version}/include/stddef.h"
    "MacOSX.sdk/usr/include/gentest_sdk_sentinel.h"
    "SDKROOT"
    "tests/consumer/bazel_private_case_value.hpp"
    "tests/consumer/bazel_dep_case_value.hpp"
    "tests/consumer/bazel_mock_dep/mock_dep.hpp"
    "GENTEST_BAZEL_MOCK_DEP=1"
    "compile_commands.json")
  string(FIND "${_module_aquery_out}" "${_required_module_action_token}" _required_module_action_pos)
  if(_required_module_action_pos EQUAL -1)
    message(FATAL_ERROR
      "Bazel module codegen action is missing '${_required_module_action_token}'.\n${_module_aquery_out}")
  endif()
endforeach()
string(FIND "${_module_aquery_out}" "no-remote" _module_staged_no_remote_pos)
string(FIND "${_module_aquery_out}" "no-cache" _module_staged_no_cache_pos)
if(NOT _module_staged_no_remote_pos EQUAL -1 OR NOT _module_staged_no_cache_pos EQUAL -1)
  message(FATAL_ERROR "Packaged-label module codegen unexpectedly disables remote execution/cache.\n${_module_aquery_out}")
endif()

function(_gentest_disk_cache_build output_root out_var)
  set(_execution_log "${output_root}/execution-log.json")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
            "${_bazel}"
            --output_user_root=${output_root}
            --max_idle_secs=5
            build
            --subcommands
            --execution_log_json_file=${_execution_log}
            ${_common_args}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err)
  if(NOT _build_rc EQUAL 0)
    message(FATAL_ERROR "Bazel disk-cache build failed.\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
  endif()
  if(NOT EXISTS "${_execution_log}")
    message(FATAL_ERROR "Bazel did not write execution log ${_execution_log}")
  endif()
  set(${out_var} "${_build_out}\n${_build_err}" PARENT_SCOPE)
endfunction()

function(_gentest_assert_codegen_cache_state execution_log expected_cache_hit phase)
  string(REPLACE "\n}{\n" ";" _action_records "${execution_log}")
  foreach(_mnemonic IN ITEMS
      GentestTextualMocksCodegen
      GentestTextualSuiteCodegen
      GentestModuleMocksCodegen
      GentestModuleSuiteCodegen)
    set(_matching_record "")
    foreach(_action_record IN LISTS _action_records)
      string(FIND "${_action_record}" "\"mnemonic\": \"${_mnemonic}\"" _mnemonic_pos)
      if(NOT _mnemonic_pos EQUAL -1)
        set(_matching_record "${_action_record}")
        break()
      endif()
    endforeach()
    if(_matching_record STREQUAL "")
      message(FATAL_ERROR "${phase}: execution log has no ${_mnemonic} entry.\n${execution_log}")
    endif()

    string(FIND "${_matching_record}" "\"runner\": \"disk cache hit\"" _disk_runner_pos)
    string(FIND "${_matching_record}" "\"cacheHit\": true" _cache_hit_pos)
    if(expected_cache_hit)
      if(_disk_runner_pos EQUAL -1 OR _cache_hit_pos EQUAL -1)
        message(FATAL_ERROR "${phase}: ${_mnemonic} did not come from disk cache.\n${_matching_record}")
      endif()
    elseif(NOT _disk_runner_pos EQUAL -1 OR NOT _cache_hit_pos EQUAL -1)
      message(FATAL_ERROR "${phase}: ${_mnemonic} unexpectedly came from disk cache.\n${_matching_record}")
    endif()
  endforeach()
endfunction()

function(_gentest_shutdown_bazel output_root)
  execute_process(
    COMMAND "${_bazel}" --output_user_root=${output_root} shutdown
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _shutdown_rc
    OUTPUT_QUIET
    ERROR_QUIET)
  if(NOT _shutdown_rc EQUAL 0)
    message(WARNING "Bazel server shutdown failed for ${output_root}; --max_idle_secs=5 remains the fallback cleanup.")
  endif()
endfunction()

_gentest_disk_cache_build("${_output_one}" _first_build_log)
string(FIND "${_first_build_log}" "--textual-wrapper-output" _first_codegen_pos)
if(_first_codegen_pos EQUAL -1)
  message(FATAL_ERROR "Initial staged-tool build did not execute the textual codegen action.\n${_first_build_log}")
endif()
file(READ "${_output_one}/execution-log.json" _first_execution_log)
_gentest_assert_codegen_cache_state("${_first_execution_log}" FALSE "initial build")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_bazel_env}
          "${_bazel}"
          --output_user_root=${_output_one}
          --max_idle_secs=5
          info bazel-bin
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _bazel_bin_rc
  OUTPUT_VARIABLE _bazel_bin
  ERROR_VARIABLE _bazel_bin_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _bazel_bin_rc EQUAL 0)
  message(FATAL_ERROR "Failed to locate staged-tool bazel-bin.\n${_bazel_bin_err}")
endif()
set(_module_compdb "${_bazel_bin}/gen/gentest_consumer_module_bazel/compile_commands.json")
if(NOT EXISTS "${_module_compdb}")
  message(FATAL_ERROR "Module suite did not materialize compile_commands.json: ${_module_compdb}")
endif()
file(READ "${_module_compdb}" _module_compdb_content)
string(REGEX MATCH "\"arguments\": \\[\"[^\"]*/bin/clang\\+\\+\"" _module_compdb_exec_clang "${_module_compdb_content}")
if(_module_compdb_exec_clang STREQUAL "")
  message(FATAL_ERROR
    "Module compile_commands.json does not use the declared exec-platform clang label.\n${_module_compdb_content}")
endif()

_gentest_disk_cache_build("${_output_two}" _second_build_log)
file(READ "${_output_two}/execution-log.json" _second_execution_log)
_gentest_assert_codegen_cache_state("${_second_execution_log}" TRUE "second isolated-output-base build")

# A declared Clang resource closure change must invalidate the action cache.
file(APPEND "${_tool_repo}/lib/clang/${_clang_resource_version}/include/stddef.h" "\n// staged Clang resource input changed\n")
_gentest_disk_cache_build("${_output_three}" _changed_build_log)
string(FIND "${_changed_build_log}" "--textual-wrapper-output" _changed_codegen_pos)
file(READ "${_output_three}/execution-log.json" _changed_execution_log)
if(_changed_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "Changing a declared exec-tool runtime input did not rerun Gentest textual codegen.\n${_changed_build_log}")
endif()
_gentest_assert_codegen_cache_state("${_changed_execution_log}" FALSE "declared-runtime-input change")

foreach(_output_root IN ITEMS "${_output_one}" "${_output_two}" "${_output_three}" "${_output_migration}" "${_output_local}")
  _gentest_shutdown_bazel("${_output_root}")
endforeach()
file(REMOVE_RECURSE "${_root}")
