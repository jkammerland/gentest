if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckTsanDeathSkipGate.cmake: SOURCE_DIR not set")
endif()

set(_tests_file "${SOURCE_DIR}/tests/CMakeLists.txt")
if(NOT EXISTS "${_tests_file}")
  message(FATAL_ERROR "Missing tests file: ${_tests_file}")
endif()

set(_presets_file "${SOURCE_DIR}/CMakePresets.json")
if(NOT EXISTS "${_presets_file}")
  message(FATAL_ERROR "Missing presets file: ${_presets_file}")
endif()

file(READ "${_tests_file}" _tests_content)
file(READ "${_presets_file}" _json)

foreach(_required IN ITEMS
    "option(GENTEST_SKIP_TSAN_DEATH_TESTS"
    "if(GENTEST_SKIP_TSAN_DEATH_TESTS)")
  string(FIND "${_tests_content}" "${_required}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "tests/CMakeLists.txt must contain the TSan death-test skip gate fragment:\n${_required}")
  endif()
endforeach()

string(FIND "${_tests_content}" "if(GENTEST_SKIP_TSAN_DEATH_TESTS)" _gate_if_pos)
if(_gate_if_pos EQUAL -1)
  message(FATAL_ERROR
    "tests/CMakeLists.txt must disable the TSan-incompatible concurrency death tests inside if(GENTEST_SKIP_TSAN_DEATH_TESTS).")
endif()

string(SUBSTRING "${_tests_content}" ${_gate_if_pos} -1 _tests_from_gate)
string(FIND "${_tests_from_gate}" "endif()" _gate_endif_rel)
if(_gate_endif_rel EQUAL -1)
  message(FATAL_ERROR
    "tests/CMakeLists.txt has an unterminated if(GENTEST_SKIP_TSAN_DEATH_TESTS) block.")
endif()

math(EXPR _gate_block_len "${_gate_endif_rel} + 7")
string(SUBSTRING "${_tests_from_gate}" 0 ${_gate_block_len} _tsan_gate_block)

string(FIND "${_tsan_gate_block}" "set_tests_properties(" _set_tests_pos)
if(_set_tests_pos EQUAL -1)
  message(FATAL_ERROR
    "TSan death-test skip gate must call set_tests_properties(... DISABLED TRUE).\nObserved block:\n${_tsan_gate_block}")
endif()
string(FIND "${_tsan_gate_block}" "PROPERTIES DISABLED TRUE" _disabled_true_pos)
if(_disabled_true_pos EQUAL -1)
  message(FATAL_ERROR
    "TSan death-test skip gate must set PROPERTIES DISABLED TRUE.\nObserved block:\n${_tsan_gate_block}")
endif()

foreach(_death_test IN ITEMS
    concurrency_fail_single_death
    concurrency_skip_no_token_death
    concurrency_xfail_no_token_death
    concurrency_multi_noadopt_death)
  string(FIND "${_tsan_gate_block}" "${_death_test}" _death_test_pos)
  if(_death_test_pos EQUAL -1)
    message(FATAL_ERROR
      "TSan death-test skip gate must disable '${_death_test}'.\nObserved block:\n${_tsan_gate_block}")
  endif()
endforeach()

function(_find_named_preset array_name preset_name out_index)
  string(JSON _len LENGTH "${_json}" ${array_name})
  if(_len LESS 1)
    message(FATAL_ERROR "No ${array_name} found in ${_presets_file}")
  endif()

  set(_found -1)
  math(EXPR _last "${_len}-1")
  foreach(_i RANGE 0 ${_last})
    string(JSON _name GET "${_json}" ${array_name} ${_i} name)
    if(_name STREQUAL "${preset_name}")
      set(_found ${_i})
      break()
    endif()
  endforeach()

  set(${out_index} ${_found} PARENT_SCOPE)
endfunction()

function(_assert_tsan_skip_enabled preset_name)
  _find_named_preset("configurePresets" "${preset_name}" _preset_index)
  if(_preset_index EQUAL -1)
    message(FATAL_ERROR "configurePresets does not contain name='${preset_name}'")
  endif()

  string(JSON _skip_value ERROR_VARIABLE _skip_error GET "${_json}" configurePresets ${_preset_index} cacheVariables GENTEST_SKIP_TSAN_DEATH_TESTS)
  if(NOT _skip_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR
      "Configure preset '${preset_name}' must set cacheVariables.GENTEST_SKIP_TSAN_DEATH_TESTS=ON")
  endif()
  if(NOT _skip_value STREQUAL "ON")
    message(FATAL_ERROR
      "Configure preset '${preset_name}' must set cacheVariables.GENTEST_SKIP_TSAN_DEATH_TESTS=ON (got '${_skip_value}')")
  endif()
endfunction()

_assert_tsan_skip_enabled("tsan")
_assert_tsan_skip_enabled("tsan-system")
