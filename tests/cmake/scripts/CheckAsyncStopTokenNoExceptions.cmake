# Compiles tests/regressions/async_stop_token_no_exceptions_probe.cpp with
# exceptions disabled. The token-free waits must compile; every explicit
# stop-token overload must be rejected with the documented static_assert.
foreach(_required IN ITEMS BUILD_ROOT SOURCE_DIR CXX_COMPILER)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckAsyncStopTokenNoExceptions.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
set(_source "${_source_dir_norm}/tests/regressions/async_stop_token_no_exceptions_probe.cpp")
set(_message "stop-token overloads require exception support")

set(_extra_args)
gentest_append_host_apple_sysroot_compile_args(_extra_args)
if(DEFINED CXX_COMPILER_TARGET AND NOT "${CXX_COMPILER_TARGET}" STREQUAL "" AND CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
  list(APPEND _extra_args "--target=${CXX_COMPILER_TARGET}")
endif()
if(DEFINED CXX_SYSROOT AND NOT "${CXX_SYSROOT}" STREQUAL "")
  list(APPEND _extra_args "--sysroot=${CXX_SYSROOT}")
endif()
if(DEFINED CXX_FLAGS AND NOT "${CXX_FLAGS}" STREQUAL "")
  separate_arguments(_configured_cxx_flags NATIVE_COMMAND "${CXX_FLAGS}")
  list(APPEND _extra_args ${_configured_cxx_flags})
endif()

function(_gentest_compile_probe out_rc out_text name)
  gentest_make_compile_only_command_args(
    _compile_args
    COMPILER "${CXX_COMPILER}"
    COMPILER_ARG1 "${CXX_COMPILER_ARG1}"
    STD "-std=c++20"
    SOURCE "${_source}"
    OBJECT "${_work_dir}/${name}.o"
    INCLUDE_ARGS "-I${_source_dir_norm}/include"
    EXTRA_ARGS ${_extra_args} ${ARGN})
  execute_process(
    COMMAND ${_compile_args}
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
  set(${out_rc} "${_rc}" PARENT_SCOPE)
  set(${out_text} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

_gentest_compile_probe(_rc _text baseline -fno-exceptions)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Token-free async waits must compile without exceptions:\n${_text}")
endif()

# With exceptions enabled every overload stays available.
foreach(_probe RANGE 1 6)
  _gentest_compile_probe(_rc _text "exceptions_${_probe}" "-DPROBE=${_probe}")
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Stop-token overload probe ${_probe} must compile with exceptions enabled:\n${_text}")
  endif()
endforeach()

foreach(_probe RANGE 1 6)
  _gentest_compile_probe(_rc _text "no_exceptions_${_probe}" -fno-exceptions "-DPROBE=${_probe}")
  if(_rc EQUAL 0)
    message(FATAL_ERROR "Stop-token overload probe ${_probe} compiled without exceptions; it would abort when cancelled")
  endif()
  string(FIND "${_text}" "${_message}" _position)
  if(_position EQUAL -1)
    message(FATAL_ERROR "Stop-token overload probe ${_probe} failed without the documented diagnostic:\n${_text}")
  endif()
endforeach()
