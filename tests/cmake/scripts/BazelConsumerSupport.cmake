include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

function(gentest_bazel_macos_clang_sdksettings_incompatible out_var is_apple clang_major)
  set(_incompatible FALSE)
  if(is_apple AND clang_major MATCHES "^[0-9]+$" AND clang_major GREATER_EQUAL 23)
    set(_incompatible TRUE)
  endif()
  set(${out_var} "${_incompatible}" PARENT_SCOPE)
endfunction()

function(gentest_skip_unsupported_bazel_consumer compiler_path)
  if(GENTEST_BAZEL_HELPER_CONTRACT)
    return()
  endif()

  gentest_clang_major_version(_clang_major "${compiler_path}")
  gentest_bazel_macos_clang_sdksettings_incompatible(
    _incompatible "${APPLE}" "${_clang_major}")
  if(_incompatible)
    gentest_skip_test(
      "Bazel's local macOS C++ toolchain does not declare Clang ${_clang_major}'s SDKSettings dependency; "
      "Gentest's Bazel contracts remain enabled, and consumer execution remains covered by earlier Clang versions.")
    set(GENTEST_BAZEL_CONSUMER_UNSUPPORTED TRUE PARENT_SCOPE)
  endif()
endfunction()
