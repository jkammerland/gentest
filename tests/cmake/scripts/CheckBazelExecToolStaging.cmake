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
  "${_work_dir}/staged")
file(WRITE "${_work_dir}/sdk/Ruby.framework/Versions/A/Headers/ruby/ruby.h" "#pragma once\n")
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

file(WRITE "${_work_dir}/llvm/bin/clang++" "fake clang\n")
file(WRITE "${_work_dir}/llvm/lib/libclang-cpp.dylib" "fake runtime\n")
gentest_stage_apple_clang_runtime("${_work_dir}/llvm/bin/clang++" "${_work_dir}/staged")
if(NOT EXISTS "${_work_dir}/staged/lib/libclang-cpp.dylib")
  message(FATAL_ERROR "Adjacent libclang-cpp.dylib was not included in the staged runtime closure")
endif()

file(READ "${SOURCE_DIR}/bazel/local_exec_tools.bzl" _local_tools)
foreach(_required IN ITEMS
    "repository_ctx.symlink(sdkroot, \"MacOSX.sdk\")"
    "macos_sdk_root = \"MacOSX.sdk/{sdk_marker}\"")
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
string(FIND "${_gentest_rules}" "\"no-sandbox\": \"1\"" _no_sandbox_pos)
if(_no_sandbox_pos EQUAL -1)
  message(FATAL_ERROR "Local exec-tool actions must remain unsandboxed when using the host SDK symlink")
endif()

message(STATUS "Bazel exec-tool staging regression passed")
