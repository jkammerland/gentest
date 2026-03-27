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

string(FIND "${_content}" "gentest_consumer_textual_bazel" _bazel_consumer_textual_pos)
if(_bazel_consumer_textual_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must validate the Bazel textual explicit-mock consumer slice.")
endif()

string(FIND "${_content}" "./bazel-bin/gentest_consumer_textual_bazel --list" _bazel_textual_list_pos)
if(_bazel_textual_list_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must run the Bazel textual consumer listing after building it.")
endif()

string(FIND "${_content}" "gentest_consumer_module_bazel" _bazel_consumer_module_pos)
if(_bazel_consumer_module_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must validate the Bazel module consumer slice.")
endif()

string(FIND "${_content}" "--experimental_cpp_modules" _bazel_cpp_modules_flag_pos)
if(_bazel_cpp_modules_flag_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must enable Bazel C++20 modules for the module consumer slice.")
endif()

string(FIND "${_content}" "./bazel-bin/gentest_consumer_module_bazel --list" _bazel_module_list_pos)
if(_bazel_module_list_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must run the Bazel module consumer listing after building it.")
endif()

foreach(_consumer_run IN ITEMS
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_mock --kind=test"
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_bench --kind=bench"
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_jitter --kind=jitter"
    "./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_mock --kind=test"
    "./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_bench --kind=bench"
    "./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_jitter --kind=jitter")
  string(FIND "${_content}" "${_consumer_run}" _consumer_run_pos)
  if(_consumer_run_pos EQUAL -1)
    message(FATAL_ERROR "buildsystems_linux workflow must execute '${_consumer_run}'.")
  endif()
endforeach()

string(FIND "${_content}" "GENTEST_CODEGEN_RESOURCE_DIR" _bazel_resource_dir_pos)
if(_bazel_resource_dir_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must wire the explicit Clang resource dir into the Bazel module consumer lane.")
endif()

string(FIND "${_content}" "gentest_consumer_textual_xmake" _xmake_consumer_textual_pos)
if(_xmake_consumer_textual_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must validate the Xmake textual explicit-mock consumer slice.")
endif()

string(FIND "${_content}" "gentest_consumer_module_xmake" _xmake_consumer_module_pos)
if(_xmake_consumer_module_pos EQUAL -1)
  message(FATAL_ERROR "buildsystems_linux workflow must validate the Xmake module consumer slice.")
endif()

foreach(_xmake_binary_var IN ITEMS "consumer_textual_bin" "consumer_module_bin")
  string(FIND "${_content}" "${_xmake_binary_var}=\"$(find " _xmake_binary_pos)
  if(_xmake_binary_pos EQUAL -1)
    message(FATAL_ERROR "buildsystems_linux workflow must resolve ${_xmake_binary_var} before executing the Xmake consumer binaries.")
  endif()
endforeach()

foreach(_literal IN ITEMS
    [[consumer_textual_bin]]
    [[consumer_module_bin]]
    [[--list]]
    [[--run=consumer/consumer/module_mock --kind=test]]
    [[--run=consumer/consumer/module_bench --kind=bench]]
    [[--run=consumer/consumer/module_jitter --kind=jitter]])
  string(FIND "${_content}" "${_literal}" _literal_pos)
  if(_literal_pos EQUAL -1)
    message(FATAL_ERROR "buildsystems_linux workflow must contain '${_literal}' for the Xmake consumer execution path.")
  endif()
endforeach()

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
set(_bazel_root_file "${SOURCE_DIR}/BUILD.bazel")
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

file(READ "${_meson_file}" _meson_content)
string(FIND "${_meson_content}" "meson_consumer_textual" _meson_consumer_textual_pos)
if(_meson_consumer_textual_pos EQUAL -1)
  message(FATAL_ERROR "meson.build must keep the textual explicit-mock consumer slice wired.")
endif()

string(FIND "${_meson_content}" "'--kind', 'textual'" _meson_kind_textual_pos)
if(_meson_kind_textual_pos EQUAL -1)
  message(FATAL_ERROR "meson.build must pass the explicit textual kind to the shared helper.")
endif()

file(READ "${_xmake_file}" _xmake_content)
string(FIND "${_xmake_content}" "gentest_consumer_module_mocks_xmake" _xmake_module_mocks_pos)
if(_xmake_module_mocks_pos EQUAL -1)
  message(FATAL_ERROR "xmake.lua must keep the repo-local module mock target wired.")
endif()

string(FIND "${_xmake_content}" "gentest_consumer_module_xmake" _xmake_module_consumer_pos)
if(_xmake_module_consumer_pos EQUAL -1)
  message(FATAL_ERROR "xmake.lua must keep the repo-local module consumer target wired.")
endif()

string(FIND "${_xmake_content}" "kind = \"modules\"" _xmake_kind_modules_pos)
if(_xmake_kind_modules_pos EQUAL -1)
  message(FATAL_ERROR "xmake.lua must keep explicit kind='modules' wiring for the repo-local module path.")
endif()

string(FIND "${_xmake_content}" "module_name = \"gentest.consumer_mocks\"" _xmake_module_name_pos)
if(_xmake_module_name_pos EQUAL -1)
  message(FATAL_ERROR "xmake.lua must keep the repo-local module mock aggregate name wired.")
endif()

string(FIND "${_xmake_content}" "GENTEST_CONSUMER_USE_MODULES=1" _xmake_module_define_pos)
if(_xmake_module_define_pos EQUAL -1)
  message(FATAL_ERROR "xmake.lua must keep the repo-local module consumer main-switch define wired.")
endif()

if(NOT EXISTS "${_bazel_root_file}")
  message(FATAL_ERROR "Missing Bazel root file: ${_bazel_root_file}")
endif()

file(READ "${_bazel_root_file}" _bazel_root_content)
string(FIND "${_bazel_root_content}" "gentest_codegen_build_unix" _bazel_codegen_rule_pos)
if(_bazel_codegen_rule_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must define the Bazel gentest_codegen bootstrap rule.")
endif()

string(FIND "${_bazel_root_content}" "CMAKE_EXPORT_COMPILE_COMMANDS=ON" _bazel_compdb_pos)
if(_bazel_compdb_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must export a compile_commands.json alongside the Bazel-built gentest_codegen tool.")
endif()

string(FIND "${_bazel_root_content}" "gentest_consumer_module_mocks" _bazel_module_mocks_pos)
if(_bazel_module_mocks_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must keep the repo-local Bazel module mock target wired.")
endif()

string(FIND "${_bazel_root_content}" "gentest_consumer_module_bazel" _bazel_module_consumer_pos)
if(_bazel_module_consumer_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must keep the repo-local Bazel module consumer target wired.")
endif()

string(FIND "${_bazel_root_content}" "gentest_add_mocks_modules(" _bazel_add_module_macro_use_pos)
if(_bazel_add_module_macro_use_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must exercise the repo-local Bazel module mock macro.")
endif()

string(FIND "${_bazel_root_content}" "gentest_attach_codegen_modules(" _bazel_attach_module_macro_use_pos)
if(_bazel_attach_module_macro_use_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must exercise the repo-local Bazel module suite macro.")
endif()

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

string(FIND "${_helper_content}" "\"textual-mocks\"" _helper_textual_mocks_pos)
if(_helper_textual_mocks_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must support the textual explicit-mock mode.")
endif()

string(FIND "${_helper_content}" "\"--kind\"" _helper_kind_pos)
if(_helper_kind_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must expose an explicit kind argument.")
endif()

string(FIND "${_helper_content}" "\"modules\"" _helper_modules_pos)
if(_helper_modules_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must expose the modules kind, even when unsupported.")
endif()

string(FIND "${_helper_content}" "os.path.relpath" _helper_relpath_pos)
if(_helper_relpath_pos EQUAL -1)
  message(FATAL_ERROR "Shared non-CMake codegen helper must derive shim includes relative to the generated wrapper.")
endif()

file(READ "${_bazel_file}" _bazel_content)

string(FIND "${_bazel_content}" "def gentest_add_mocks_modules(" _bazel_module_macro_pos)
if(_bazel_module_macro_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must define the repo-local Bazel module mock macro.")
endif()

string(FIND "${_bazel_content}" "def gentest_attach_codegen_modules(" _bazel_attach_module_macro_pos)
if(_bazel_attach_module_macro_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must define the repo-local Bazel module suite macro.")
endif()

string(FIND "${_bazel_content}" "compile_commands.json" _bazel_module_compdb_pos)
if(_bazel_module_compdb_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must keep the repo-local Bazel module compile_commands handoff.")
endif()

string(FIND "${_bazel_content}" "_mock_metadata" _bazel_module_metadata_pos)
if(_bazel_module_metadata_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must keep the repo-local Bazel module metadata handoff.")
endif()
