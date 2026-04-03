if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBazelModuleMockHeaderPropagation.cmake: SOURCE_DIR not set")
endif()

set(_build_defs_file "${SOURCE_DIR}/build_defs/gentest.bzl")
if(NOT EXISTS "${_build_defs_file}")
  message(FATAL_ERROR "Expected Bazel rule definitions are missing: ${_build_defs_file}")
endif()

set(_build_file "${SOURCE_DIR}/BUILD.bazel")
if(NOT EXISTS "${_build_file}")
  message(FATAL_ERROR "Expected Bazel BUILD file is missing: ${_build_file}")
endif()

set(_check_script "${SOURCE_DIR}/cmake/CheckBazelBzlmodConsumer.cmake")
if(NOT EXISTS "${_check_script}")
  message(FATAL_ERROR "Expected Bazel Bzlmod consumer check is missing: ${_check_script}")
endif()

file(READ "${_build_defs_file}" _build_defs_content)
string(FIND "${_build_defs_content}" "-fmodules-embed-all-files" _build_defs_embed_pos)
if(_build_defs_embed_pos EQUAL -1)
  message(FATAL_ERROR
    "build_defs/gentest.bzl must embed textual headers into Bazel-produced C++ module artifacts.\n"
    "Expected to find -fmodules-embed-all-files in the exported module rule copts.")
endif()

file(READ "${_build_file}" _build_content)
string(FIND "${_build_content}" "_gentest_public_module_copts = _gentest_public_copts + ['-fmodules-embed-all-files']" _build_embed_pos)
if(_build_embed_pos EQUAL -1)
  message(FATAL_ERROR
    "BUILD.bazel must compile the exported gentest module targets with -fmodules-embed-all-files.\n"
    "The public gentest/gentest.mock/gentest.bench_util modules need sandbox-stable PCM contents too.")
endif()

file(READ "${_check_script}" _check_content)
string(FIND "${_check_content}" "--strategy=CppCompile=local" _strategy_pos)
if(NOT _strategy_pos EQUAL -1)
  message(FATAL_ERROR
    "CheckBazelBzlmodConsumer.cmake should no longer rely on --strategy=CppCompile=local.\n"
    "The exported Bazel rules must make the macOS module-mock consumer path sandbox-safe on their own.")
endif()
