# Skipping bodies invents unused-private-field diagnostics and hides real body errors.
foreach(_required IN ITEMS BUILD_ROOT GENTEST_SOURCE_DIR PROG DRIVER_MODE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckFunctionBodyDiagnostics.cmake: ${_required} not set")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clangxx)
  gentest_skip_test("function-body diagnostics: host Clang not found")
  return()
endif()

if(DRIVER_MODE STREQUAL "cl")
  set(_flags --driver-mode=cl /std:c++20 -TP /W4 /WX -Wunused-private-field)
  set(_syntax_only /Zs)
  set(_diagnostic_pragma "")
elseif(DRIVER_MODE STREQUAL "g++")
  set(_flags
      --driver-mode=g++
      -std=c++20
      -x
      c++
      -Wall
      -Wextra
      -Werror)
  set(_syntax_only -fsyntax-only)
  # GNU -Werror is already stripped by codegen; source-local promotion proves late diagnostics remain effective. In cl mode /WX alone must promote it.
  set(_diagnostic_pragma "#pragma clang diagnostic error \"-Wunused-private-field\"\n")
else()
  message(FATAL_ERROR "Unsupported DRIVER_MODE: ${DRIVER_MODE}")
endif()
# This synthetic command selects its own driver; distro clang++.cfg defaults can inject GNU-only flags into the cl-mode compiler oracle.
list(PREPEND _flags --no-default-config)
list(APPEND _flags -Wno-unknown-attributes)

foreach(_slot IN ITEMS authored-tu fallback-header)
  foreach(_scenario IN ITEMS used unused body_error)
    set(_work "${BUILD_ROOT}/function_body_diagnostics/${_slot}/${_scenario}")
    file(MAKE_DIRECTORY "${_work}/generated")
    set(_getter "return value_;")
    set(_expected_error "")
    if(_scenario STREQUAL "unused")
      set(_getter "return 7;")
      set(_expected_error "error: private field 'value_' is not used")
    elseif(_scenario STREQUAL "body_error")
      set(_getter "return value_ + body_error_sentinel;")
      set(_expected_error "error: use of undeclared identifier 'body_error_sentinel'")
    endif()
    gentest_fixture_write_file("${_work}/cases.hpp" "#pragma once\n${_diagnostic_pragma}" "class BodyRead {\n  int value_ = 7;\npublic:\n  int get() const { ${_getter} }\n};\n"
                               "[[using gentest: test(\"body/read\")]] inline void body_read() { BodyRead value; (void)value.get(); }\n")
    gentest_fixture_write_file("${_work}/cases.cpp" "#include \"cases.hpp\"\n")

    # The ordinary compiler is the diagnostic oracle, not codegen's own output.
    execute_process(
      COMMAND "${_clangxx}" ${_flags} ${_syntax_only} "${_work}/cases.cpp"
      RESULT_VARIABLE _compile_rc
      OUTPUT_VARIABLE _compile_out
      ERROR_VARIABLE _compile_err)
    if(_expected_error STREQUAL "")
      if(NOT _compile_rc EQUAL 0 OR "${_compile_out}${_compile_err}" MATCHES "warning:|error:")
        message(FATAL_ERROR "Valid fixture must compile cleanly:\n${_compile_out}\n${_compile_err}")
      endif()
    elseif(_compile_rc EQUAL 0 OR NOT "${_compile_out}${_compile_err}" MATCHES "${_expected_error}")
      message(FATAL_ERROR "Invalid fixture must fail with ${_expected_error}:\n${_compile_out}\n${_compile_err}")
    endif()

    set(_output "${_work}/generated/registration.cpp")
    set(_manifest "${_work}/generated/manifest.json")
    if(_slot STREQUAL "authored-tu")
      set(_input "${_work}/cases.cpp")
      set(_command_file "${_input}")
    else()
      set(_input "${_work}/cases.hpp")
      set(_command_file "${_output}")
    endif()
    gentest_fixture_make_compdb_entry(
      _entry
      DIRECTORY
      "${_work}"
      FILE
      "${_command_file}"
      ARGUMENTS
      "${_clangxx}"
      ${_flags}
      -c
      --
      "${_command_file}")
    gentest_fixture_write_compdb("${_work}/compile_commands.json" "${_entry}")
    file(REMOVE "${_output}" "${_manifest}")
    execute_process(
      COMMAND "${PROG}" --compdb "${_work}" --host-clang "${_clangxx}" --tu-out-dir "${_work}/generated" --artifact-manifest "${_manifest}" --scan-slot-kind "${_slot}" --textual-registration-output
              "${_output}" "${_input}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err)
    if(NOT _expected_error STREQUAL "")
      if(_rc EQUAL 0 OR NOT "${_out}${_err}" MATCHES "${_expected_error}")
        message(FATAL_ERROR "Expected ${_slot}/${_scenario} to fail with ${_expected_error}:\n${_out}\n${_err}")
      endif()
      if(EXISTS "${_manifest}" OR EXISTS "${_output}")
        message(FATAL_ERROR "Failed scan published registration artifacts")
      endif()
    else()
      if(NOT _rc EQUAL 0 OR "${_out}${_err}" MATCHES "warning:|error:")
        message(FATAL_ERROR "Expected clean ${_slot}/${_scenario} scan:\n${_out}\n${_err}")
      endif()
      file(READ "${_output}" _text)
      string(REGEX MATCHALL "\"body/read\"" _matches "${_text}")
      list(LENGTH _matches _count)
      if(NOT _count EQUAL 1 OR NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "Expected one body/read registration and an artifact manifest")
      endif()
    endif()
  endforeach()
endforeach()
