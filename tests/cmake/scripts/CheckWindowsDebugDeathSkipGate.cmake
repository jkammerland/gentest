# Lints Windows Debug death-test skip gating so it works for multi-config generators.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckWindowsDebugDeathSkipGate.cmake: SOURCE_DIR not set")
endif()

set(_legacy_gate "WIN32 AND CMAKE_BUILD_TYPE STREQUAL \"Debug\" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS")
set(_workflow_file "${SOURCE_DIR}/.github/workflows/cmake.yml")
set(_files
    "${SOURCE_DIR}/tests/CMakeLists.txt"
    "${SOURCE_DIR}/tests/cmake/Regressions.cmake")

set(_legacy_hits)
set(_missing_config_genex)
foreach(_file IN LISTS _files)
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Missing file: ${_file}")
  endif()

  file(READ "${_file}" _content)

  string(FIND "${_content}" "${_legacy_gate}" _legacy_pos)
  if(NOT _legacy_pos EQUAL -1)
    list(APPEND _legacy_hits "${_file}")
  endif()

  string(FIND "${_content}" "$<CONFIG:Debug>" _config_genex_pos)
  if(_config_genex_pos EQUAL -1)
    list(APPEND _missing_config_genex "${_file}")
  endif()
endforeach()

if(_legacy_hits)
  string(JOIN "\n  " _hits ${_legacy_hits})
  message(FATAL_ERROR
    "Found legacy Windows Debug skip gate based on CMAKE_BUILD_TYPE (ineffective for multi-config):\n  ${_hits}")
endif()

if(_missing_config_genex)
  string(JOIN "\n  " _missing ${_missing_config_genex})
  message(FATAL_ERROR
    "Expected config-aware skip gate using $<CONFIG:Debug> in:\n  ${_missing}")
endif()

if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _workflow_content)

set(_expected_workflow_gate [=[if ("${{ matrix.preset }}" -in @("debug-system", "debug-system-cxx23")) {]=])
string(FIND "${_workflow_content}" "${_expected_workflow_gate}" _workflow_gate_pos)
if(_workflow_gate_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected Windows workflow to apply GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS to both debug-system and debug-system-cxx23 presets.\n"
    "Missing snippet: ${_expected_workflow_gate}")
endif()

string(FIND "${_workflow_content}" "-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON" _workflow_flag_pos)
if(_workflow_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected Windows workflow to pass -DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON during configure.")
endif()
