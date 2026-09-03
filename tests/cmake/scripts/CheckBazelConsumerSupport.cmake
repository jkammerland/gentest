include("${CMAKE_CURRENT_LIST_DIR}/BazelConsumerSupport.cmake")

function(_gentest_expect_incompatible name is_apple clang_major expected)
  gentest_bazel_macos_clang_sdksettings_incompatible(
    _actual "${is_apple}" "${clang_major}")
  if(NOT _actual STREQUAL "${expected}")
    message(FATAL_ERROR
      "${name}: expected '${expected}', got '${_actual}' for Apple=${is_apple}, Clang=${clang_major}")
  endif()
endfunction()

_gentest_expect_incompatible(macos_clang_22 TRUE 22 FALSE)
_gentest_expect_incompatible(macos_clang_23 TRUE 23 TRUE)
_gentest_expect_incompatible(macos_future_clang TRUE 24 TRUE)
_gentest_expect_incompatible(non_macos_clang_23 FALSE 23 FALSE)
_gentest_expect_incompatible(macos_unknown_compiler TRUE "" FALSE)
