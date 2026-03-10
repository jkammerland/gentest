if(DEFINED RUN_PROBE)
  if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "CheckMissingHostCodegenPath.cmake probe: SOURCE_DIR not set")
  endif()

  set(CMAKE_CROSSCOMPILING TRUE)
  set(GENTEST_CODEGEN_EXECUTABLE "${SOURCE_DIR}/this/path/does/not/exist/gentest_codegen")
  include("${SOURCE_DIR}/cmake/GentestCodegen.cmake")
  _gentest_resolve_codegen_backend(TARGET probe OUT_CODEGEN_TARGET _target OUT_CODEGEN_EXECUTABLE _exe)
  message(FATAL_ERROR "Expected missing host codegen path to be rejected")
endif()

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckMissingHostCodegenPath.cmake: SOURCE_DIR not set")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${SOURCE_DIR}"
    "-DRUN_PROBE=ON"
    -P "${CMAKE_CURRENT_LIST_FILE}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR "GentestCodegen.cmake accepted a nonexistent GENTEST_CODEGEN_EXECUTABLE")
endif()

set(_all "${_out}\n${_err}")
string(FIND "${_all}" "does not exist" _msg_pos)
if(_msg_pos EQUAL -1)
  message(FATAL_ERROR "Unexpected failure while probing missing host codegen path:\n${_all}")
endif()
