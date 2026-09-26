# A listed-header scan must honor header guards even when its borrowed command explicitly selects C++ source mode (as CMake/clang-cl does with -TP).
foreach(_required IN ITEMS BUILD_ROOT GENTEST_SOURCE_DIR PROG)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckFallbackHeaderSemantics.cmake: ${_required} not set")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clangxx)
  gentest_skip_test("fallback-header semantics: host Clang not found")
  return()
endif()

get_filename_component(_compiler_name "${_clangxx}" NAME_WE)
set(_warning_modes native)
set(_source_flags -std=c++20 -x c++)
if(_compiler_name STREQUAL "clang-cl" OR _compiler_name STREQUAL "cl")
  set(_warning_modes /WX -WX)
  set(_source_flags /std:c++20 -TP)
endif()

set(_root "${BUILD_ROOT}/fallback_header_semantics")
foreach(_warning_mode IN LISTS _warning_modes)
  string(REPLACE "/" "slash" _mode_dir "${_warning_mode}")
  foreach(_scenario IN ITEMS single multi parse_error warning_error authored_source)
    if(_warning_mode STREQUAL "native" AND (_scenario STREQUAL "warning_error" OR _scenario STREQUAL "authored_source"))
      continue()
    endif()
    set(_work "${_root}/${_mode_dir}/${_scenario}")
    file(MAKE_DIRECTORY "${_work}/generated")
    # Match the helper's existing treatment of Gentest's custom attributes; all unrelated diagnostics and warnings-as-errors remain active.
    set(_flags ${_source_flags} -Wno-unknown-attributes)
    if(NOT _warning_mode STREQUAL "native")
      list(APPEND _flags "${_warning_mode}")
    endif()
    gentest_fixture_write_file(
      "${_work}/once.hpp"
      [=[
#pragma once
#include "once.hpp"
[[using gentest: test("header/once")]] inline void header_once() {}
]=])
    gentest_fixture_write_file(
      "${_work}/second.hpp"
      [=[
#pragma once
#include "second.hpp"
[[using gentest: test("header/second")]] inline void header_second() {}
]=])
    # This authored input does not reach either header, so multi-slot execution exercises both fallback parses rather than skipping already seen headers.
    gentest_fixture_write_file("${_work}/authored.cpp" "void unrelated() {}\n")
    set(_inputs "${_work}/once.hpp")
    set(_kinds fallback-header)
    set(_expected_error "")
    if(_scenario STREQUAL "multi")
      set(_inputs "${_work}/authored.cpp" "${_work}/once.hpp" "${_work}/second.hpp")
      set(_kinds authored-tu fallback-header fallback-header)
    elseif(_scenario STREQUAL "parse_error")
      gentest_fixture_write_file("${_work}/once.hpp" "#pragma once\nstatic_assert(false, \"header_parse_sentinel\");\n")
      set(_expected_error "header_parse_sentinel")
    elseif(_scenario STREQUAL "warning_error")
      gentest_fixture_write_file("${_work}/once.hpp" "#pragma once\n#warning header_warning_sentinel\n")
      set(_expected_error "error: header_warning_sentinel")
    elseif(_scenario STREQUAL "authored_source")
      gentest_fixture_write_file("${_work}/authored.cpp" "#pragma once\nvoid unrelated() {}\n")
      set(_inputs "${_work}/authored.cpp")
      set(_kinds authored-tu)
      set(_expected_error "error: #pragma once in main file")
    endif()

    set(_entries)
    set(_outputs)
    set(_args)
    list(LENGTH _inputs _count)
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE 0 ${_last})
      list(GET _kinds ${_index} _kind)
      set(_output "${_work}/generated/slot_${_index}.cpp")
      list(APPEND _outputs "${_output}")
      list(APPEND _args --scan-slot-kind "${_kind}" --textual-registration-output "${_output}")
      gentest_fixture_make_compdb_entry(
        _entry
        DIRECTORY
        "${_work}"
        FILE
        "${_output}"
        ARGUMENTS
        "${_clangxx}"
        ${_flags}
        -c
        --
        "${_output}")
      list(APPEND _entries "${_entry}")
    endforeach()
    gentest_fixture_write_compdb("${_work}/compile_commands.json" ${_entries})
    set(_manifest "${_work}/generated/manifest.json")
    file(REMOVE ${_outputs} "${_manifest}")
    execute_process(
      COMMAND "${PROG}" --compdb "${_work}" --host-clang "${_clangxx}" --tu-out-dir "${_work}/generated" --artifact-manifest "${_manifest}" --jobs=2 ${_args} ${_inputs}
      WORKING_DIRECTORY "${_work}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err)
    if(NOT _expected_error STREQUAL "")
      if(_rc EQUAL 0 OR NOT "${_out}${_err}" MATCHES "${_expected_error}")
        message(FATAL_ERROR "Expected ${_warning_mode}/${_scenario} to fail with ${_expected_error}:\n${_out}\n${_err}")
      endif()
      if(EXISTS "${_manifest}")
        message(FATAL_ERROR "Failed scan published an artifact manifest")
      endif()
      continue()
    endif()
    if(NOT _rc EQUAL 0 OR "${_out}${_err}" MATCHES "warning:|error:")
      message(FATAL_ERROR "Expected clean ${_warning_mode}/${_scenario} scan:\n${_out}\n${_err}")
    endif()
    set(_generated "")
    foreach(_output IN LISTS _outputs)
      file(READ "${_output}" _text)
      string(APPEND _generated "${_text}")
    endforeach()
    set(_names once)
    if(_scenario STREQUAL "multi")
      list(APPEND _names second)
    endif()
    foreach(_name IN LISTS _names)
      string(REGEX MATCHALL "\"header/${_name}\"" _matches "${_generated}")
      list(LENGTH _matches _matches_count)
      if(NOT _matches_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one header/${_name} registration, got ${_matches_count}")
      endif()
    endforeach()
    file(READ "${_manifest}" _json)
    string(JSON _source_count LENGTH "${_json}" sources)
    if(NOT _source_count EQUAL _count)
      message(FATAL_ERROR "Wrong manifest source count for ${_warning_mode}/${_scenario}")
    endif()
  endforeach()
endforeach()
