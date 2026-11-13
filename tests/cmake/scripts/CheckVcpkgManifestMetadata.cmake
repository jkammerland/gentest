# Requires:
#  -DPROJECT_SOURCE_DIR=<repo root>

set(_project_root "")
if(DEFINED PROJECT_SOURCE_DIR AND NOT "${PROJECT_SOURCE_DIR}" STREQUAL "")
  set(_project_root "${PROJECT_SOURCE_DIR}")
elseif(DEFINED SOURCE_DIR AND NOT "${SOURCE_DIR}" STREQUAL "")
  set(_project_root "${SOURCE_DIR}")
else()
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: PROJECT_SOURCE_DIR or SOURCE_DIR must be set")
endif()

set(_cmake_lists "${_project_root}/CMakeLists.txt")
set(_manifest "${_project_root}/vcpkg.json")
set(_config "${_project_root}/vcpkg-configuration.json")

if(NOT EXISTS "${_cmake_lists}")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: missing ${_cmake_lists}")
endif()
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: missing ${_manifest}")
endif()

file(READ "${_cmake_lists}" _cmake_text)
if(NOT _cmake_text MATCHES "project\\([^)]*VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: unable to parse project version from ${_cmake_lists}")
endif()
set(_project_version "${CMAKE_MATCH_1}")

file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_version ERROR_VARIABLE _manifest_version_error GET "${_manifest_json}" version)
if(_manifest_version_error)
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: unable to read 'version' from ${_manifest}: ${_manifest_version_error}")
endif()

if(NOT _manifest_version STREQUAL _project_version)
  message(FATAL_ERROR
    "CheckVcpkgManifestMetadata.cmake: vcpkg manifest version '${_manifest_version}' does not match project version '${_project_version}'")
endif()

# The other build systems restate the version; none of them may drift from CMake.
function(_gentest_check_restated_version relative_path pattern description)
  set(_path "${_project_root}/${relative_path}")
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: missing ${_path}")
  endif()
  file(READ "${_path}" _text)
  if(NOT _text MATCHES "${pattern}")
    message(FATAL_ERROR
      "CheckVcpkgManifestMetadata.cmake: unable to find ${description} in ${_path}")
  endif()
  if(NOT CMAKE_MATCH_1 STREQUAL _project_version)
    message(FATAL_ERROR
      "CheckVcpkgManifestMetadata.cmake: ${description} in ${relative_path} is '${CMAKE_MATCH_1}', "
      "which does not match project version '${_project_version}'")
  endif()
endfunction()

_gentest_check_restated_version(MODULE.bazel
  "module\\([ \t\r\n]*name[ \t]*=[ \t]*\"gentest\"[ \t]*,[ \t]*version[ \t]*=[ \t]*\"([^\"]*)\""
  "the Bazel module version")
_gentest_check_restated_version(BUILD.bazel
  "\n_gentest_version[ \t]*=[ \t]*'([^']*)'"
  "the Bazel runtime version (_gentest_version)")
_gentest_check_restated_version(meson.build
  "project\\([ \t\r\n]*'gentest'[^)]*version[ \t]*:[ \t]*'([^']*)'"
  "the Meson project version")
_gentest_check_restated_version(xmake.lua
  "\nlocal gentest_version[ \t]*=[ \t]*\"([^\"]*)\"[ \t]*\r?\nset_version\\(gentest_version\\)"
  "the xmake project version (gentest_version passed to set_version)")

# Every backend must hand the runtime that version for `--help`.
foreach(_define_use IN ITEMS
    "src/CMakeLists.txt|GENTEST_VERSION_STR=\"\${PROJECT_VERSION}\""
    "BUILD.bazel|'GENTEST_VERSION_STR=\\\\\"' + _gentest_version"
    "meson.build|-DGENTEST_VERSION_STR=\"@0@\"'.format(meson.project_version())"
    "xmake.lua|\"GENTEST_VERSION_STR=\\\"\" .. gentest_version")
  string(REPLACE "|" ";" _define_use_parts "${_define_use}")
  list(GET _define_use_parts 0 _define_file)
  list(GET _define_use_parts 1 _define_text)
  file(READ "${_project_root}/${_define_file}" _define_file_text)
  string(FIND "${_define_file_text}" "${_define_text}" _define_pos)
  if(_define_pos EQUAL -1)
    message(FATAL_ERROR
      "CheckVcpkgManifestMetadata.cmake: ${_define_file} must define GENTEST_VERSION_STR for the runtime from its "
      "project version (expected to find: ${_define_text})")
  endif()
endforeach()

set(_baseline "")
string(JSON _builtin_baseline ERROR_VARIABLE _builtin_baseline_error GET "${_manifest_json}" builtin-baseline)
if(NOT _builtin_baseline_error AND NOT "${_builtin_baseline}" STREQUAL "")
  set(_baseline "${_builtin_baseline}")
endif()

if("${_baseline}" STREQUAL "" AND EXISTS "${_config}")
  file(READ "${_config}" _config_json)
  string(JSON _config_baseline ERROR_VARIABLE _config_baseline_error GET "${_config_json}" default-registry baseline)
  if(NOT _config_baseline_error AND NOT "${_config_baseline}" STREQUAL "")
    set(_baseline "${_config_baseline}")
  endif()
endif()

if("${_baseline}" STREQUAL "")
  message(FATAL_ERROR
    "CheckVcpkgManifestMetadata.cmake: no vcpkg baseline found in vcpkg.json or vcpkg-configuration.json")
endif()

if(UNIX)
  find_program(_bash_program bash)
  find_program(_git_program git)
  if(_bash_program AND _git_program)
    function(_gentest_vcpkg_run label)
      execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE _run_rc
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)
      if(NOT _run_rc EQUAL 0)
        message(FATAL_ERROR
          "CheckVcpkgManifestMetadata.cmake: ${label} failed (${_run_rc}).\n"
          "stdout:\n${_run_out}\n"
          "stderr:\n${_run_err}")
      endif()
    endfunction()

    set(_repin_work_dir "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_repin_regression")
    set(_repin_remote_dir "${_repin_work_dir}/remote")
    set(_repin_checkout_dir "${_repin_work_dir}/checkout")
    set(_repin_project_dir "${_repin_work_dir}/project")
    file(REMOVE_RECURSE "${_repin_work_dir}")
    file(MAKE_DIRECTORY "${_repin_remote_dir}" "${_repin_project_dir}/scripts")

    _gentest_vcpkg_run("initializing local vcpkg remote" "${_git_program}" init "${_repin_remote_dir}")
    _gentest_vcpkg_run("configuring local vcpkg author" "${_git_program}" -C "${_repin_remote_dir}" config user.email "gentest@example.invalid")
    _gentest_vcpkg_run("configuring local vcpkg author name" "${_git_program}" -C "${_repin_remote_dir}" config user.name "gentest")
    file(WRITE "${_repin_remote_dir}/bootstrap-vcpkg.sh" [=[#!/usr/bin/env bash
set -euo pipefail
printf 'bootstrapped\n' > bootstrap-marker.txt
]=])
    file(CHMOD "${_repin_remote_dir}/bootstrap-vcpkg.sh"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(WRITE "${_repin_remote_dir}/baseline.txt" "old\n")
    _gentest_vcpkg_run("staging old vcpkg revision" "${_git_program}" -C "${_repin_remote_dir}" add bootstrap-vcpkg.sh baseline.txt)
    _gentest_vcpkg_run("committing old vcpkg revision" "${_git_program}" -C "${_repin_remote_dir}" commit -m old)
    execute_process(
      COMMAND "${_git_program}" -C "${_repin_remote_dir}" rev-parse HEAD
      OUTPUT_VARIABLE _old_vcpkg_revision
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    file(WRITE "${_repin_remote_dir}/baseline.txt" "new\n")
    _gentest_vcpkg_run("staging baseline vcpkg revision" "${_git_program}" -C "${_repin_remote_dir}" add baseline.txt)
    _gentest_vcpkg_run("committing baseline vcpkg revision" "${_git_program}" -C "${_repin_remote_dir}" commit -m baseline)
    execute_process(
      COMMAND "${_git_program}" -C "${_repin_remote_dir}" rev-parse HEAD
      OUTPUT_VARIABLE _new_vcpkg_revision
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    _gentest_vcpkg_run("cloning local vcpkg checkout" "${_git_program}" clone "${_repin_remote_dir}" "${_repin_checkout_dir}")
    _gentest_vcpkg_run("selecting old vcpkg revision" "${_git_program}" -C "${_repin_checkout_dir}" checkout --detach "${_old_vcpkg_revision}")
    file(WRITE "${_repin_checkout_dir}/vcpkg" "old binary\n")

    file(COPY "${_project_root}/scripts/setup-vcpkg.sh" DESTINATION "${_repin_project_dir}/scripts")
    file(WRITE "${_repin_project_dir}/vcpkg-configuration.json"
      "{\"default-registry\":{\"kind\":\"builtin\",\"baseline\":\"${_new_vcpkg_revision}\"}}\n")
    set(_github_env_file "${_repin_work_dir}/github-env.txt")
    set(_github_path_file "${_repin_work_dir}/github-path.txt")
    file(WRITE "${_github_env_file}" "")
    file(WRITE "${_github_path_file}" "")

    set(_setup_command
      "${CMAKE_COMMAND}" -E env
      "VCPKG_ROOT=${_repin_checkout_dir}"
      "GITHUB_ACTIONS=true"
      "RUNNER_OS=Linux"
      "GITHUB_ENV=${_github_env_file}"
      "GITHUB_PATH=${_github_path_file}"
      "GENTEST_VCPKG_REPOSITORY=${_repin_remote_dir}"
      "${_bash_program}" "${_repin_project_dir}/scripts/setup-vcpkg.sh")
    _gentest_vcpkg_run("repinning an existing vcpkg binary" ${_setup_command})

    execute_process(
      COMMAND "${_git_program}" -C "${_repin_checkout_dir}" rev-parse HEAD
      OUTPUT_VARIABLE _actual_vcpkg_revision
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _actual_vcpkg_revision STREQUAL _new_vcpkg_revision)
      message(FATAL_ERROR
        "CheckVcpkgManifestMetadata.cmake: repin selected '${_actual_vcpkg_revision}', expected '${_new_vcpkg_revision}'")
    endif()
    if(NOT EXISTS "${_repin_checkout_dir}/bootstrap-marker.txt")
      message(FATAL_ERROR
        "CheckVcpkgManifestMetadata.cmake: repinning with an existing binary did not rerun bootstrap-vcpkg.sh")
    endif()

    file(REMOVE "${_repin_checkout_dir}/bootstrap-marker.txt")
    _gentest_vcpkg_run("reusing an already pinned vcpkg binary" ${_setup_command})
    if(EXISTS "${_repin_checkout_dir}/bootstrap-marker.txt")
      message(FATAL_ERROR
        "CheckVcpkgManifestMetadata.cmake: an unchanged checkout with an existing binary was bootstrapped again")
    endif()
  endif()
endif()

message(STATUS "vcpkg metadata looks consistent (version=${_manifest_version}, baseline=${_baseline})")
