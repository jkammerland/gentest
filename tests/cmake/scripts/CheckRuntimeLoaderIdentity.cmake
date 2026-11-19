# Inspect the linked executable, including imports the loader cannot resolve.
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${CONSUMER}"
  DIRECTORIES "${RUNTIME_DIR}"
  PRE_INCLUDE_REGEXES "gentest_runtime"
  PRE_EXCLUDE_REGEXES ".*"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved)

if(STATIC_RUNTIME)
  cmake_path(GET RUNTIME FILENAME _archive)
  if(NOT _archive STREQUAL EXPECTED_ARCHIVE OR NOT EXISTS "${RUNTIME}" OR _resolved OR _unresolved)
    message(FATAL_ERROR "Expected ${EXPECTED_ARCHIVE} and no runtime import, got ${RUNTIME}; ${_resolved}; ${_unresolved}")
  endif()
  return()
endif()

if(CMAKE_HOST_WIN32)
  set(_identity "${DLL_PREFIX}gentest_runtime-2${POSTFIX}.dll")
elseif(CMAKE_HOST_APPLE)
  set(_identity "libgentest_runtime${POSTFIX}.2.dylib")
else()
  set(_identity "libgentest_runtime${POSTFIX}.so.2")
endif()
if(NOT EXISTS "${RUNTIME_DIR}/${_identity}" OR _unresolved)
  message(FATAL_ERROR "Missing runtime ${_identity} in ${RUNTIME_DIR}; unresolved imports: ${_unresolved}")
endif()
list(LENGTH _resolved _count)
if(NOT _count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one Gentest runtime import, got '${_resolved}'")
endif()
cmake_path(GET _resolved FILENAME _loaded_name)
if(NOT _loaded_name STREQUAL _identity)
  message(FATAL_ERROR "Consumer imports '${_loaded_name}', expected '${_identity}'")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  # Run a copy without its build-tree search path. The only loadable Gentest
  # library here has the 1.3 name. An empty file masks any system ABI 2 copy.
  set(_legacy "${RUNTIME_DIR}/legacy")
  file(MAKE_DIRECTORY "${_legacy}")
  file(COPY_FILE "${CONSUMER}" "${_legacy}/consumer")
  file(RPATH_REMOVE FILE "${_legacy}/consumer")
  file(CREATE_LINK "${RUNTIME}" "${_legacy}/libgentest_runtime${POSTFIX}.so" SYMBOLIC)
  file(WRITE "${_legacy}/${_identity}" "")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${_legacy}" "${_legacy}/consumer"
    RESULT_VARIABLE _status ERROR_VARIABLE _error)
  string(FIND "${_error}" "${_identity}" _match)
  if(_status EQUAL 0 OR _match EQUAL -1)
    message(FATAL_ERROR "Expected loader failure for ${_identity}, got ${_status}: ${_error}")
  endif()
elseif(CMAKE_HOST_APPLE)
  execute_process(COMMAND otool -D "${RUNTIME}" COMMAND_ERROR_IS_FATAL ANY OUTPUT_VARIABLE _id)
  string(FIND "${_id}" "@rpath/${_identity}\n" _match)
  if(_match EQUAL -1)
    message(FATAL_ERROR "Unexpected Mach-O install name:\n${_id}")
  endif()
endif()
