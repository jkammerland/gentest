#[[
  Contract check for public module consumer docs.

  Runtime/package proof of the supported module-consumer flow already lives in
  the checked-in `tests/consumer` smoke and the package/non-CMake helper tests.
  This script keeps the user-facing docs aligned with that supported link
  contract so the published snippets do not drift independently.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckModuleDocContract.cmake: SOURCE_DIR not set")
endif()

set(_consumer_file "${SOURCE_DIR}/tests/consumer/CMakeLists.txt")
set(_readme_file "${SOURCE_DIR}/README.md")
set(_modules_file "${SOURCE_DIR}/docs/modules.md")
set(_story_file "${SOURCE_DIR}/docs/stories/010_public_modules_progress_report.md")

foreach(_required IN ITEMS
    "${_consumer_file}"
    "${_readme_file}"
    "${_modules_file}"
    "${_story_file}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing module-doc contract input: ${_required}")
  endif()
endforeach()

file(READ "${_consumer_file}" _consumer_content)
foreach(_expected IN ITEMS
    "target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_main)"
    "target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_runtime)"
    "target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_main gentest::gentest_runtime)")
  string(FIND "${_consumer_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "tests/consumer/CMakeLists.txt is missing supported module link contract token: ${_expected}")
  endif()
endforeach()

file(READ "${_readme_file}" _readme_content)
string(REGEX MATCH
  "target_link_libraries\\(module_tests PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]+service_mocks\\)"
  _readme_module_link "${_readme_content}")
if(_readme_module_link STREQUAL "")
  message(FATAL_ERROR
    "README.md must show module consumers linking `gentest::gentest` alongside `gentest::gentest_runtime` in the named-module mock example.")
endif()

file(READ "${_modules_file}" _modules_content)
string(REGEX MATCH
  "target_link_libraries\\(my_tests PRIVATE[ \t\r\n]+gentest::gentest[ \t\r\n]+gentest::gentest_runtime[ \t\r\n]+my_service_mocks[ \t\r\n]*\\)"
  _modules_link "${_modules_content}")
if(_modules_link STREQUAL "")
  message(FATAL_ERROR
    "docs/modules.md must show `import gentest;` consumers linking `gentest::gentest` in the explicit mock example.")
endif()

foreach(_expected IN ITEMS
    "`import gentest;` consumers should link `gentest::gentest`, not just `gentest::gentest_main`."
    "[docs/stories/010_public_modules_progress_report.md](stories/010_public_modules_progress_report.md)")
  string(FIND "${_modules_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR "docs/modules.md is missing module-doc contract token: ${_expected}")
  endif()
endforeach()

file(READ "${_story_file}" _story_content)
foreach(_forbidden IN ITEMS
    "module consumers can link just `gentest::gentest_main`"
    "`gentest_main` bring in `gentest_runtime`"
    "proves the `gentest_main`-only path")
  string(FIND "${_story_content}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR
      "docs/stories/010_public_modules_progress_report.md still contains stale module link guidance: ${_forbidden}")
  endif()
endforeach()

foreach(_expected IN ITEMS
    "module consumers should link `gentest::gentest`"
    "`gentest_main` still provides the main entrypoint/runtime side")
  string(FIND "${_story_content}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "docs/stories/010_public_modules_progress_report.md is missing refreshed module link guidance token: ${_expected}")
  endif()
endforeach()
