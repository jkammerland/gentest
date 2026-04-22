# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleDetailHidden.cmake: GENTEST_SOURCE_DIR not set")
endif()

set(_module_path "${GENTEST_SOURCE_DIR}/include/gentest/gentest.cppm")
if(NOT EXISTS "${_module_path}")
  message(FATAL_ERROR "CheckPublicModuleDetailHidden.cmake: module interface not found at '${_module_path}'")
endif()

file(READ "${_module_path}" _module_text)

set(_forbidden_exports
  "using ::gentest::detail::SharedFixtureScope"
  "using ::gentest::detail::SharedFixtureRegistration"
  "using ::gentest::detail::get_shared_fixture"
  "using ::gentest::detail::register_cases"
  "using ::gentest::detail::register_shared_fixture"
  "using ::gentest::detail::setup_shared_fixtures"
  "using ::gentest::detail::snapshot_registered_cases"
  "using ::gentest::detail::teardown_shared_fixtures")

foreach(_forbidden IN LISTS _forbidden_exports)
  string(FIND "${_module_text}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR
      "Public module interface should not explicitly export internal runtime plumbing.\n"
      "Forbidden export still present: ${_forbidden}")
  endif()
endforeach()

message(STATUS "Observed trimmed explicit gentest::detail exports in public module interface")
