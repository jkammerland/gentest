if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBuildsystemsLinuxWorkflow.cmake: SOURCE_DIR not set")
endif()

set(_workflow_file "${SOURCE_DIR}/.github/workflows/buildsystems_linux.yml")
if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _content)

string(FIND "${_content}" "dnf -y install \\" _dnf_install_pos)
if(_dnf_install_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must install Fedora dependencies with dnf.")
endif()

string(FIND "${_content}" "xmake" _xmake_pkg_pos)
if(_xmake_pkg_pos EQUAL -1)
  message(FATAL_ERROR "Fedora buildsystems workflow must install packaged xmake.")
endif()

string(FIND "${_content}" [[if [ "${{ matrix.pkg_manager }}" = "dnf" ]; then]] _dnf_branch_pos)
if(_dnf_branch_pos EQUAL -1)
  message(FATAL_ERROR "Xmake workflow must special-case the Fedora package-manager path.")
endif()

string(FIND "${_content}" [[xmake_bin="$(command -v xmake)"]] _xmake_bin_pos)
if(_xmake_bin_pos EQUAL -1)
  message(FATAL_ERROR "Xmake workflow must resolve the system xmake binary for Fedora runs.")
endif()

string(FIND "${_content}" [[su - ci -c "\"${xmake_bin}\" --version"]] _xmake_ci_version_pos)
if(_xmake_ci_version_pos EQUAL -1)
  message(FATAL_ERROR "Fedora xmake workflow must run the packaged xmake binary as the ci user.")
endif()

string(FIND "${_content}" "/home/ci/.local/bin/xmake f -c -m release" _hardcoded_build_pos)
if(NOT _hardcoded_build_pos EQUAL -1)
  message(FATAL_ERROR "Xmake build step must not hardcode the ci-local xmake path.")
endif()

string(FIND "${_content}" "/home/ci/.local/bin/xmake r gentest_unit_xmake" _hardcoded_test_pos)
if(NOT _hardcoded_test_pos EQUAL -1)
  message(FATAL_ERROR "Xmake test step must not hardcode the ci-local xmake path.")
endif()

set(_meson_file "${SOURCE_DIR}/meson.build")
set(_xmake_file "${SOURCE_DIR}/xmake.lua")
set(_bazel_file "${SOURCE_DIR}/build_defs/gentest.bzl")
set(_helper_file "${SOURCE_DIR}/scripts/gentest_buildsystem_codegen.py")

foreach(_buildsystem_file IN LISTS _meson_file _xmake_file _bazel_file)
  if(NOT EXISTS "${_buildsystem_file}")
    message(FATAL_ERROR "Missing buildsystem integration file: ${_buildsystem_file}")
  endif()

  file(READ "${_buildsystem_file}" _buildsystem_content)

  string(FIND "${_buildsystem_content}" "gentest_buildsystem_codegen.py" _helper_ref_pos)
  if(_helper_ref_pos EQUAL -1)
    message(FATAL_ERROR "${_buildsystem_file} must use the shared non-CMake per-TU codegen helper.")
  endif()

  string(FIND "${_buildsystem_content}" "--output" _manifest_pos)
  if(NOT _manifest_pos EQUAL -1)
    message(FATAL_ERROR "${_buildsystem_file} must not use legacy gentest_codegen --output manifest mode.")
  endif()

  string(FIND "${_buildsystem_content}" "--tu-out-dir" _tu_mode_pos)
  if(NOT _tu_mode_pos EQUAL -1)
    message(FATAL_ERROR "${_buildsystem_file} should route per-TU generation through the shared helper, not inline --tu-out-dir invocations.")
  endif()
endforeach()

if(NOT EXISTS "${_helper_file}")
  message(FATAL_ERROR "Missing shared non-CMake codegen helper: ${_helper_file}")
endif()

file(READ "${_helper_file}" _helper_content)

string(FIND "${_helper_content}" "\"--output\"" _helper_manifest_pos)
if(NOT _helper_manifest_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must not route through legacy gentest_codegen --output.")
endif()

string(FIND "${_helper_content}" "\"--tu-out-dir\"" _helper_tu_mode_pos)
if(_helper_tu_mode_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must invoke gentest_codegen in per-TU mode.")
endif()

string(FIND "${_helper_content}" "os.path.relpath" _helper_relpath_pos)
if(_helper_relpath_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must derive shim includes relative to the generated wrapper.")
endif()
