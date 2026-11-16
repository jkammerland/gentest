#[[
  Contract check for public module consumer docs.

  Runtime/package proof of the supported module-consumer flow already lives in
  the checked-in `tests/consumer` smoke and the package/non-CMake helper tests.
  This script keeps the link lines published in docs/modules.md aligned with
  that supported link contract. It checks the snippets, not the prose around
  them.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckModuleDocContract.cmake: SOURCE_DIR not set")
endif()

set(_consumer_file "${SOURCE_DIR}/tests/consumer/CMakeLists.txt")
set(_modules_file "${SOURCE_DIR}/docs/modules.md")

foreach(_required IN ITEMS
    "${_consumer_file}"
    "${_modules_file}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing module-doc contract input: ${_required}")
  endif()
endforeach()

function(_assert_regex content pattern description)
  string(REGEX MATCH "${pattern}" _match "${content}")
  if(_match STREQUAL "")
    message(FATAL_ERROR "${description}")
  endif()
endfunction()

file(READ "${_consumer_file}" _consumer_content)
_assert_regex(
  "${_consumer_content}"
  "target_link_libraries\\(gentest_consumer PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_main[ \t\r\n]*\\)"
  "tests/consumer/CMakeLists.txt must keep the module main-only link contract.")
_assert_regex(
  "${_consumer_content}"
  "target_link_libraries\\(gentest_consumer PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]*\\)"
  "tests/consumer/CMakeLists.txt must keep the module runtime-only link contract.")
_assert_regex(
  "${_consumer_content}"
  "target_link_libraries\\(gentest_consumer PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_main[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]*\\)"
  "tests/consumer/CMakeLists.txt must keep the module double-link contract.")

file(READ "${_modules_file}" _modules_content)
_assert_regex(
  "${_modules_content}"
  "target_link_libraries\\(my_tests PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]*\\)"
  "docs/modules.md must keep the quick-start `import gentest;` example linking `gentest::gentest` with `gentest::gentest_runtime` when the snippet provides its own `main()`.")
_assert_regex(
  "${_modules_content}"
  "target_link_libraries\\(my_tests PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]+my_service_mocks[ \t\r\n]*\\)"
  "docs/modules.md must show `import gentest;` consumers linking `gentest::gentest` in the mock example.")
