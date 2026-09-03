include("${CMAKE_CURRENT_LIST_DIR}/BazelMacosSdkSystemInclude.cmake")

function(_gentest_expect_sdk_root name dependencies expected)
  gentest_extract_macos_sdk_root("${dependencies}" _actual)
  if(NOT _actual STREQUAL "${expected}")
    message(FATAL_ERROR
      "${name}: expected SDK root '${expected}', got '${_actual}' from '${dependencies}'")
  endif()
endfunction()

_gentest_expect_sdk_root(
  json_marker
  "probe.o: /Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/SDKSettings.json"
  "/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk")
_gentest_expect_sdk_root(
  plist_marker
  "probe.o: /Platforms/MacOSX.sdk/SDKSettings.plist"
  "/Platforms/MacOSX.sdk")
_gentest_expect_sdk_root(
  escaped_space
  "probe.o: /Applications/Xcode\\ Beta.app/SDKs/MacOSX.sdk/SDKSettings.json"
  "/Applications/Xcode Beta.app/SDKs/MacOSX.sdk")
_gentest_expect_sdk_root(no_marker "probe.o: /usr/include/stddef.h" "")
