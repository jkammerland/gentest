if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: PROG not set")
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake module consumer smoke check.")
  return()
endif()

set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/tmp_xmake_module_consumer")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN=${_codegen}"
          "${_xmake}" f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE _cfg_err)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake configure failed for the module consumer smoke check.\n"
    "stdout:\n${_cfg_out}\n"
    "stderr:\n${_cfg_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN=${_codegen}"
          "${_xmake}" build -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -y
          gentest_consumer_module_xmake
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_module_xmake.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

file(GLOB_RECURSE _consumer_bins
  LIST_DIRECTORIES FALSE
  "${_out_dir}/*gentest_consumer_module_xmake"
  "${_out_dir}/*gentest_consumer_module_xmake.exe")
list(LENGTH _consumer_bins _consumer_bin_count)
if(NOT _consumer_bin_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one built Xmake module consumer binary, found ${_consumer_bin_count}.\n"
    "Candidates:\n${_consumer_bins}")
endif()
list(GET _consumer_bins 0 _consumer_bin)

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE _run_err)
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer failed.\n"
    "stdout:\n${_run_out}\n"
    "stderr:\n${_run_err}")
endif()

foreach(_expected IN ITEMS
    "consumer/consumer/module_test"
    "consumer/consumer/module_mock"
    "consumer/consumer/module_bench"
    "consumer/consumer/module_jitter")
  string(FIND "${_run_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "The Xmake module consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_run_out}")
  endif()
endforeach()
