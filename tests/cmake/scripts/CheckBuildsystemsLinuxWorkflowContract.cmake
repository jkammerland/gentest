#[[
  Contract-only check for the Linux workflow shape. Runtime proof of the native
  non-CMake backends lives in the direct Meson/Xmake/Bazel smoke tests.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBuildsystemsLinuxWorkflowContract.cmake: SOURCE_DIR not set")
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
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_test --kind=test"
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_mock --kind=test"
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_bench --kind=bench"
    "./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_jitter --kind=jitter"
    "./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_test --kind=test"
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

foreach(_explicit_host_clang_literal IN ITEMS
    [[export GENTEST_CODEGEN_HOST_CLANG="${host_clang}"]]
    [[--action_env=GENTEST_CODEGEN_HOST_CLANG \]]
    [[--host_action_env=GENTEST_CODEGEN_HOST_CLANG \]]
    [[--repo_env=GENTEST_CODEGEN_HOST_CLANG \]]
    [[-Dcodegen_host_clang="${host_clang}"]]
    [[GENTEST_CODEGEN_HOST_CLANG="${host_clang}"]])
  string(FIND "${_content}" "${_explicit_host_clang_literal}" _explicit_host_clang_literal_pos)
  if(_explicit_host_clang_literal_pos EQUAL -1)
    message(FATAL_ERROR
      "buildsystems_linux workflow must contain '${_explicit_host_clang_literal}' to exercise the explicit host-clang contract.")
  endif()
endforeach()

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
    [[--run=consumer/consumer/module_test --kind=test]]
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
set(_xmake_helper_file "${SOURCE_DIR}/xmake/gentest.lua")
set(_bazel_file "${SOURCE_DIR}/build_defs/gentest.bzl")
set(_bazel_root_file "${SOURCE_DIR}/BUILD.bazel")

foreach(_required_file IN ITEMS
    "${_meson_file}"
    "${_xmake_file}"
    "${_xmake_helper_file}"
    "${_bazel_file}"
    "${_bazel_root_file}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR "Missing buildsystem integration file: ${_required_file}")
  endif()
endforeach()

file(READ "${_meson_file}" _meson_content)
string(FIND "${_meson_content}" "gentest_buildsystem_codegen.py" _meson_helper_pos)
if(NOT _meson_helper_pos EQUAL -1)
  message(FATAL_ERROR "meson.build must not route textual codegen through scripts/gentest_buildsystem_codegen.py anymore.")
endif()
foreach(_expected IN ITEMS
    "meson_consumer_textual"
    "Meson named-module support is"
    "'--tu-out-dir'"
    "'--textual-wrapper-output'"
    "'--mock-public-header'"
    "'--depfile'"
    "'--artifact-manifest'"
    "'--artifact-owner-source'"
    "'--compile-context-id'"
    "depfile:")
  string(FIND "${_meson_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "meson.build is missing expected native textual token: ${_expected}")
  endif()
endforeach()

file(READ "${_xmake_file}" _xmake_content)
file(READ "${_xmake_helper_file}" _xmake_helper_content)
foreach(_xmake_blob IN ITEMS "_xmake_content" "_xmake_helper_content")
  string(FIND "${${_xmake_blob}}" "gentest_buildsystem_codegen.py" _helper_ref_pos)
  if(NOT _helper_ref_pos EQUAL -1)
    message(FATAL_ERROR "Xmake integration must not reference scripts/gentest_buildsystem_codegen.py anymore (${_xmake_blob}).")
  endif()
endforeach()
foreach(_expected IN ITEMS
    "gentest_consumer_module_mocks_xmake"
    "gentest_consumer_module_xmake"
    "GENTEST_CONSUMER_USE_MODULES=1")
  string(FIND "${_xmake_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake.lua is missing expected native Xmake token: ${_expected}")
  endif()
endforeach()
foreach(_expected IN ITEMS
    "function gentest_add_mocks(opts)"
    "function gentest_attach_codegen(opts)"
    "run_command(batchcmds, codegen, args)"
    "module_public_output_rel"
    "module_registration_output_rel"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "\"--artifact-owner-source\""
    "\"--compile-context-id\""
    "registered_target_metadata()"
    "collect_mock_metadata_inputs")
  string(FIND "${_xmake_helper_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "xmake/gentest.lua is missing expected native helper token: ${_expected}")
  endif()
endforeach()

if(NOT EXISTS "${_bazel_root_file}")
  message(FATAL_ERROR "Missing Bazel root file: ${_bazel_root_file}")
endif()

file(READ "${_bazel_root_file}" _bazel_root_content)
string(FIND "${_bazel_root_content}" "gentest_codegen_build_unix" _bazel_codegen_rule_pos)
if(_bazel_codegen_rule_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must define the Bazel gentest_codegen bootstrap rule.")
endif()

foreach(_stale_compdb_copy IN ITEMS
    [[cp $(@D)/b/compile_commands.json $(@D)/compile_commands.json]]
    [[copy /Y $(@D)\b\compile_commands.json $(@D)\compile_commands.json >NUL]])
  string(FIND "${_bazel_root_content}" "${_stale_compdb_copy}" _stale_compdb_copy_pos)
  if(NOT _stale_compdb_copy_pos EQUAL -1)
    message(FATAL_ERROR "BUILD.bazel must not advertise a copied tool-adjacent Bazel compile_commands contract anymore: ${_stale_compdb_copy}")
  endif()
endforeach()

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

string(FIND "${_bazel_root_content}" "defs_modules = [" _bazel_defs_modules_pos)
if(_bazel_defs_modules_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must pass explicit defs_modules for the repo-local Bazel module mock macro.")
endif()

string(FIND "${_bazel_root_content}" "gentest_attach_codegen_modules(" _bazel_attach_module_macro_use_pos)
if(_bazel_attach_module_macro_use_pos EQUAL -1)
  message(FATAL_ERROR "BUILD.bazel must exercise the repo-local Bazel module suite macro.")
endif()

file(READ "${_bazel_file}" _bazel_content)
string(FIND "${_bazel_content}" "gentest_buildsystem_codegen.py" _bazel_helper_pos)
if(NOT _bazel_helper_pos EQUAL -1)
  message(FATAL_ERROR "build_defs/gentest.bzl must not reference scripts/gentest_buildsystem_codegen.py anymore.")
endif()

foreach(_expected IN ITEMS
    "GentestGeneratedInfo = provider("
    "def gentest_add_mocks_modules("
    "def gentest_attach_codegen_modules("
    "_gentest_module_mocks_codegen = rule("
    "_gentest_module_suite_codegen = rule("
    "ctx.actions.expand_template("
    "ctx.actions.write("
    "ctx.actions.run("
    "use_default_shell_env = True"
    "defs_modules"
    "_gentest_module_registration_relpath"
    "\"--module-registration-output\""
    "\"--artifact-manifest\""
    "\"--artifact-owner-source\""
    "\"--compile-context-id\""
    "output_group = \"module_interfaces\""
    "output_group = \"artifact_manifests\"")
  string(FIND "${_bazel_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "build_defs/gentest.bzl is missing expected native Bazel token: ${_expected}")
  endif()
endforeach()
