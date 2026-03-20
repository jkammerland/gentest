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

string(FIND "${_content}" "/home/ci/.local/bin/xmake f -c -m release" _hardcoded_build_pos)
if(NOT _hardcoded_build_pos EQUAL -1)
  message(FATAL_ERROR "Xmake build step must not hardcode the ci-local xmake path.")
endif()

string(FIND "${_content}" "/home/ci/.local/bin/xmake r gentest_unit_xmake" _hardcoded_test_pos)
if(NOT _hardcoded_test_pos EQUAL -1)
  message(FATAL_ERROR "Xmake test step must not hardcode the ci-local xmake path.")
endif()
