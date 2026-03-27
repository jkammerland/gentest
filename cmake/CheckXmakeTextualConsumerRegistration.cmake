if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeTextualConsumerRegistration.cmake: SOURCE_DIR not set")
endif()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake consumer target registration check.")
  return()
endif()

set(_xmake_file "${SOURCE_DIR}/xmake.lua")
if(NOT EXISTS "${_xmake_file}")
  message(FATAL_ERROR "Missing xmake.lua: ${_xmake_file}")
endif()

set(_tmp_dir "${CMAKE_CURRENT_BINARY_DIR}/tmp_xmake_target_registration")
file(REMOVE_RECURSE "${_tmp_dir}")
file(MAKE_DIRECTORY "${_tmp_dir}")
file(COPY_FILE "${_xmake_file}" "${_tmp_dir}/xmake.lua")

execute_process(
  COMMAND "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -l targets
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -l targets failed for clean textual consumer registration check.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_target
    gentest_consumer_textual_mocks_xmake
    gentest_consumer_textual_xmake
    gentest_unit_xmake)
  string(FIND "${_list_out}" "${_target}" _target_pos)
  if(_target_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected Xmake target '${_target}' to be registered from a clean xmake.lua parse.\n"
      "xmake output:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -t gentest_consumer_textual_xmake
  RESULT_VARIABLE _target_rc
  OUTPUT_VARIABLE _target_out
  ERROR_VARIABLE _target_err)
if(NOT _target_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -t gentest_consumer_textual_xmake failed for clean textual consumer registration check.\n"
    "stdout:\n${_target_out}\n"
    "stderr:\n${_target_err}")
endif()

string(FIND "${_target_out}" "gentest_consumer_textual_mocks_xmake" _dep_pos)
if(_dep_pos EQUAL -1)
  message(FATAL_ERROR
    "gentest_consumer_textual_xmake should depend on gentest_consumer_textual_mocks_xmake.\n"
    "xmake target output:\n${_target_out}")
endif()
