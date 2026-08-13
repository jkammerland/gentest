if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CheckBazelExecToolStaging.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR BUILD_ROOT STREQUAL "")
  message(FATAL_ERROR "CheckBazelExecToolStaging.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/BazelExecToolStaging.cmake")

set(_work_dir "${BUILD_ROOT}/bazel_exec_tool_staging")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY
  "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby"
  "${_work_dir}/llvm/bin"
  "${_work_dir}/llvm/lib"
  "${_work_dir}/llvm/lib/c++"
  "${_work_dir}/llvm/lib/unwind"
  "${_work_dir}/staged")
file(WRITE "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby.h" "#pragma once\n")
if(WIN32)
  # Production pruning is Apple-only. Do not model its POSIX relative symlinks
  # on Windows, where CMake may create neither the link nor a resolvable target.
  file(WRITE "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby" "ordinary entry\n")
  gentest_prune_cyclic_directory_symlinks("${_work_dir}/sdk")
  if(NOT EXISTS "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby")
    message(FATAL_ERROR "Windows staging unexpectedly pruned an ordinary framework entry")
  endif()
else()
  file(CREATE_LINK "." "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby" SYMBOLIC)
  file(CREATE_LINK "A" "${_work_dir}/sdk/Ruby.framework/Versions/Current" SYMBOLIC)
  file(CREATE_LINK "Versions/Current/Headers" "${_work_dir}/sdk/Ruby.framework/Headers" SYMBOLIC)

  gentest_prune_cyclic_directory_symlinks("${_work_dir}/sdk")
  if(EXISTS "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby" OR
     IS_SYMLINK "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby")
    message(FATAL_ERROR "Cyclic Ruby.framework directory symlink was not pruned")
  endif()
  foreach(_alias IN ITEMS
      "${_work_dir}/sdk/Ruby.framework/Versions/Current"
      "${_work_dir}/sdk/Ruby.framework/Headers")
    if(NOT IS_SYMLINK "${_alias}")
      message(FATAL_ERROR "Non-cyclic framework alias was unexpectedly removed: ${_alias}")
    endif()
  endforeach()
endif()

file(WRITE "${_work_dir}/llvm/bin/clang++" "fake clang\n")
file(WRITE "${_work_dir}/llvm/lib/libclang-cpp.dylib" "fake runtime\n")
file(WRITE "${_work_dir}/llvm/lib/c++/libc++.dylib" "fake C++ runtime\n")
file(WRITE "${_work_dir}/llvm/lib/unwind/libunwind.dylib" "fake unwind runtime\n")
gentest_stage_apple_clang_runtime("${_work_dir}/llvm/bin/clang++" "${_work_dir}/staged")
foreach(_staged_runtime IN ITEMS
    lib/libclang-cpp.dylib
    lib/c++/libc++.dylib
    lib/unwind/libunwind.dylib)
  if(NOT EXISTS "${_work_dir}/staged/${_staged_runtime}")
    message(FATAL_ERROR "Bounded runtime file was not staged: ${_staged_runtime}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/bazel/local_exec_tools.bzl" _local_tools)
foreach(_required IN ITEMS
    "repository_ctx.symlink(sdkroot, \"MacOSX.sdk\")"
    "macos_sdk_root = \"MacOSX.sdk/{sdk_marker}\""
    "local_clang_path = {local_clang_path}"
    "local_macos_sdk_root = {local_macos_sdk_root}")
  string(FIND "${_local_tools}" "${_required}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR "Local macOS exec-tool bootstrap is missing '${_required}'")
  endif()
endforeach()
string(FIND "${_local_tools}" "glob([\"MacOSX.sdk/**\"])" _recursive_sdk_glob_pos)
if(NOT _recursive_sdk_glob_pos EQUAL -1)
  message(FATAL_ERROR "Local macOS SDK must not be recursively globbed through framework symlinks")
endif()

file(READ "${SOURCE_DIR}/build_defs/gentest.bzl" _gentest_rules)
foreach(_local_action_token IN ITEMS
    "\"no-cache\": \"1\""
    "\"no-remote\": \"1\""
    "\"no-sandbox\": \"1\"")
  string(FIND "${_gentest_rules}" "${_local_action_token}" _local_action_token_pos)
  if(_local_action_token_pos EQUAL -1)
    message(FATAL_ERROR "Local exec-tool action isolation is missing '${_local_action_token}'")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/bazel/toolchain.bzl" _toolchain_rules)
string(FIND "${_toolchain_rules}"
  "ctx.attr.local_macos_sdk_root if ctx.attr.local_macos_sdk_root else"
  _local_sdk_precedence_pos)
if(_local_sdk_precedence_pos EQUAL -1)
  message(FATAL_ERROR "Local macOS toolchains must export the absolute host SDK instead of the relative marker path")
endif()
string(FIND "${_toolchain_rules}"
  "clang_path = ctx.attr.local_clang_path if ctx.attr.local_clang_path else ctx.executable.clang.path"
  _local_clang_precedence_pos)
if(_local_clang_precedence_pos EQUAL -1)
  message(FATAL_ERROR "Local toolchains must invoke the absolute host Clang instead of its external-repository symlink")
endif()

file(READ "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelModuleConsumer.cmake" _module_consumer)
foreach(_module_preflight_token IN ITEMS
    "set(_clang_scan_deps \"\${_clang_bin_dir}/clang-scan-deps\")"
    "COMMAND \"\${_clang_scan_deps}\" --version"
    "GENTEST_SKIP_TEST: clang-scan-deps is unavailable beside the selected macOS Clang")
  string(FIND "${_module_consumer}" "${_module_preflight_token}" _module_preflight_pos)
  if(_module_preflight_pos EQUAL -1)
    message(FATAL_ERROR "Bazel module smoke preflight is missing '${_module_preflight_token}'")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelCodegenActionCache.cmake" _action_cache_fixture)
foreach(_resource_contract_token IN ITEMS
    "set(_staged_resource_ownership \"declared\")"
    "set(_staged_resource_ownership \"host\")"
    "glob([\"system-include/**\"], allow_empty = True)"
    "Local macOS fallback must export the absolute host SDK"
    "Packaged macOS codegen must export its declared execroot SDK")
  string(FIND "${_action_cache_fixture}" "${_resource_contract_token}" _resource_contract_pos)
  if(_resource_contract_pos EQUAL -1)
    message(FATAL_ERROR "Bazel Apple SDK/resource fixture is missing '${_resource_contract_token}'")
  endif()
endforeach()

message(STATUS "Bazel exec-tool staging regression passed")
