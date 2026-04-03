if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBazelModuleEmbedFlagContract.cmake: SOURCE_DIR not set")
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
string(REGEX MATCH
  "def _gentest_module_compile_copts\\(defines = \\[\\], clang_args = \\[\\]\\):[^\n]*\n[ \t]*return _gentest_compile_copts\\(defines, clang_args\\) \\+ \\[\"-fmodules-embed-all-files\"\\]"
  _build_defs_embed_impl
  "${_build_defs_content}")
if(_build_defs_embed_impl STREQUAL "")
  message(FATAL_ERROR
    "build_defs/gentest.bzl must keep `_gentest_module_compile_copts()` wiring `-fmodules-embed-all-files`\n"
      "into the actual exported module compile copts, not just leave the token somewhere in the file.")
endif()

function(_assert_contains content token description)
  string(FIND "${content}" "${token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR "${description} must still use '${token}'.")
  endif()
endfunction()

function(_extract_named_cc_library content name out_block)
  set(_anchor "cc_library(\n    name = '${name}'")
  string(FIND "${content}" "${_anchor}" _start)
  if(_start EQUAL -1)
    message(FATAL_ERROR "Missing cc_library() block for ${name}")
  endif()
  string(SUBSTRING "${content}" ${_start} -1 _tail)
  string(FIND "${_tail}" "\ncc_library(" _next_rel)
  if(_next_rel EQUAL -1)
    set(_block "${_tail}")
  else()
    string(SUBSTRING "${_tail}" 0 ${_next_rel} _block)
  endif()
  set(${out_block} "${_block}" PARENT_SCOPE)
endfunction()

function(_extract_function_body content function_name out_block)
  set(_anchor "def ${function_name}(")
  string(FIND "${content}" "${_anchor}" _start)
  if(_start EQUAL -1)
    message(FATAL_ERROR "Missing Starlark function '${function_name}'")
  endif()
  string(SUBSTRING "${content}" ${_start} -1 _tail)
  string(FIND "${_tail}" "\ndef " _next_rel)
  if(_next_rel EQUAL -1)
    set(_block "${_tail}")
  else()
    string(SUBSTRING "${_tail}" 0 ${_next_rel} _block)
  endif()
  set(${out_block} "${_block}" PARENT_SCOPE)
endfunction()

file(READ "${_build_file}" _build_content)
string(FIND "${_build_content}" "_gentest_public_module_copts = _gentest_public_copts + ['-fmodules-embed-all-files']" _public_module_copts_pos)
if(_public_module_copts_pos EQUAL -1)
  message(FATAL_ERROR
    "BUILD.bazel must keep `_gentest_public_module_copts` wiring `-fmodules-embed-all-files` into the\n"
    "actual public gentest module targets.")
endif()
_extract_named_cc_library("${_build_content}" "gentest" _gentest_public_module_block)
_assert_contains("${_gentest_public_module_block}" "copts = _gentest_public_module_copts" "The public gentest module target")
_extract_named_cc_library("${_build_content}" "gentest_mock" _gentest_mock_module_block)
_assert_contains("${_gentest_mock_module_block}" "copts = _gentest_public_module_copts" "The public gentest.mock module target")
_extract_named_cc_library("${_build_content}" "gentest_bench_util" _gentest_bench_util_module_block)
_assert_contains("${_gentest_bench_util_module_block}" "copts = _gentest_public_module_copts"
  "The public gentest.bench_util module target")
_extract_function_body("${_build_defs_content}" "gentest_add_mocks_modules" _gentest_add_mocks_modules_body)
_assert_contains("${_gentest_add_mocks_modules_body}" "copts = _gentest_module_compile_copts(defines, clang_args)"
  "gentest_add_mocks_modules()")
_extract_function_body("${_build_defs_content}" "gentest_attach_codegen_modules" _gentest_attach_codegen_modules_body)
_assert_contains("${_gentest_attach_codegen_modules_body}" "copts = _gentest_module_compile_copts(defines, clang_args)"
  "gentest_attach_codegen_modules()")

file(READ "${_check_script}" _check_content)
string(FIND "${_check_content}" "--strategy=CppCompile=local" _strategy_pos)
if(NOT _strategy_pos EQUAL -1)
  message(FATAL_ERROR
    "CheckBazelBzlmodConsumer.cmake should no longer rely on --strategy=CppCompile=local.\n"
    "The exported Bazel rules must make the macOS module-mock consumer path sandbox-safe on their own.")
endif()
