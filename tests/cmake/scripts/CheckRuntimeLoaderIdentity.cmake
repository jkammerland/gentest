# Read ELF imports directly. The linker records their names in DT_NEEDED,
# including on musl systems without a glibc loader cache.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${OBJDUMP}" -p "${CONSUMER}"
    COMMAND_ERROR_IS_FATAL ANY OUTPUT_VARIABLE _headers)
  # Library names can contain semicolons. Keep every matching name intact for
  # the string comparison below, which accepts exactly one expected import.
  set(_needed "\n[ \t]+NEEDED[ \t]+")
  string(REGEX MATCHALL "${_needed}[^\r\n]*gentest_runtime[^\r\n]*" _entries "${_headers}")
  string(REGEX REPLACE "${_needed}" "" _imports "${_entries}")
else()
  file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${CONSUMER}"
    DIRECTORIES "${RUNTIME_DIR}"
    PRE_INCLUDE_REGEXES "gentest_runtime"
    PRE_EXCLUDE_REGEXES ".*"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved)
  if(_unresolved)
    message(FATAL_ERROR "Unresolved Gentest runtime imports: ${_unresolved}")
  endif()
  foreach(_path IN LISTS _resolved)
    cmake_path(GET _path FILENAME _name)
    list(APPEND _imports "${_name}")
  endforeach()
endif()

if(STATIC_RUNTIME)
  cmake_path(GET RUNTIME FILENAME _archive)
  if(NOT _archive STREQUAL EXPECTED_ARCHIVE OR NOT EXISTS "${RUNTIME}" OR _imports)
    message(FATAL_ERROR "Expected ${EXPECTED_ARCHIVE} and no runtime import, got ${RUNTIME}; ${_imports}")
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
if(NOT EXISTS "${RUNTIME_DIR}/${_identity}")
  message(FATAL_ERROR "Missing runtime ${_identity} in ${RUNTIME_DIR}")
endif()
if(NOT _imports STREQUAL _identity)
  message(FATAL_ERROR "Consumer imports '${_imports}', expected '${_identity}'")
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
