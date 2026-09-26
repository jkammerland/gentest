# A compile-command delimiter is syntax, not a semantic compiler option.
foreach(_required IN ITEMS BUILD_ROOT GENTEST_SOURCE_DIR PROG DRIVER_MODE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckCodegenCompileCommandDelimiter.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clangxx)
  gentest_skip_test("compile-command delimiter regression: host Clang not found")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/compile_command_delimiter")
file(MAKE_DIRECTORY "${_work_dir}/generated")
set(_header "${_work_dir}/cases.hpp")
set(_source "${_work_dir}/cases.cpp")
set(_registration "${_work_dir}/generated/registration.cpp")
set(_manifest "${_work_dir}/generated/manifest.json")
gentest_fixture_write_file("${_header}" [=[
#pragma once
static_assert(char(-1) > 0, "unsigned-char compiler option was lost");
#if defined(GENTEST_DISABLED)
#error compile-command undefinition was lost
#endif
#if GENTEST_VARIANT == 1
[[using gentest: test("delimiter/one")]] inline void delimiter_one() {}
#elif GENTEST_VARIANT == 2
[[using gentest: test("delimiter/two")]] inline void delimiter_two() {}
#else
#error compile-command definition was lost
#endif
]=])
gentest_fixture_write_file("${_source}" "#include \"cases.hpp\"\n")

if(DRIVER_MODE STREQUAL "cl")
  set(_standard "/std:c++20")
  # In cl mode -J is a flag. Flang's same-spelled option consumes a value,
  # so it must not swallow a following delimiter while fingerprinting.
  set(_unsigned_char -J)
  set(_force_include /FI)
elseif(DRIVER_MODE STREQUAL "g++")
  set(_standard "-std=c++20")
  set(_unsigned_char -funsigned-char)
  set(_force_include -include)
else()
  message(FATAL_ERROR "Unsupported DRIVER_MODE: ${DRIVER_MODE}")
endif()
set(_driver_flags "--driver-mode=${DRIVER_MODE}" "${_standard}")

foreach(_slot IN ITEMS authored-tu fallback-header)
  if(_slot STREQUAL "authored-tu")
    set(_input "${_source}")
  else()
    set(_input "${_header}")
  endif()
  foreach(_scenario IN ITEMS plain delimited both_delimited slot_only changed)
    set(_variant 1)
    set(_separator)
    if(NOT _scenario STREQUAL "plain")
      set(_separator "--")
    endif()
    if(_scenario STREQUAL "changed")
      set(_variant 2)
    endif()
    set(_flags ${_driver_flags} "-DGENTEST_VARIANT=${_variant}" "-DGENTEST_DISABLED=1" "-UGENTEST_DISABLED")
    # Mixed commands prove equal fingerprints; marking both commands or using
    # only the generated slot also exercises a delimited selected scan command.
    gentest_fixture_make_compdb_entry(_generated_entry
      DIRECTORY "${_work_dir}" FILE "${_registration}"
      ARGUMENTS "${_clangxx}" ${_flags} -c "${_unsigned_char}" ${_separator} "${_registration}")
    set(_authored_separator)
    if(_scenario STREQUAL "both_delimited")
      set(_authored_separator "--")
    endif()
    gentest_fixture_make_compdb_entry(_authored_entry
      DIRECTORY "${_work_dir}" FILE "${_input}"
      ARGUMENTS "${_clangxx}" ${_flags} -c "${_unsigned_char}" ${_authored_separator} "${_input}")
    if(_scenario STREQUAL "slot_only")
      gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_generated_entry}")
    else()
      gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_generated_entry}" "${_authored_entry}")
    endif()
    file(REMOVE "${_registration}" "${_manifest}")
    gentest_check_run_or_fail(
      COMMAND "${PROG}" --compdb "${_work_dir}" --host-clang "${_clangxx}"
        --tu-out-dir "${_work_dir}/generated"
        --textual-registration-output "${_registration}"
        --artifact-manifest "${_manifest}" --scan-slot-kind "${_slot}" "${_input}"
      WORKING_DIRECTORY "${_work_dir}" STRIP_TRAILING_WHITESPACE)
    file(READ "${_registration}" _output)
    if(_variant EQUAL 1)
      set(_expected "delimiter/one")
      set(_absent "delimiter/two")
    else()
      set(_expected "delimiter/two")
      set(_absent "delimiter/one")
    endif()
    if(NOT _output MATCHES "${_expected}" OR _output MATCHES "${_absent}")
      message(FATAL_ERROR "Wrong registered case for ${_slot}/${_scenario}:\n${_output}")
    endif()
    file(READ "${_manifest}" _json)
    string(JSON _source_count LENGTH "${_json}" sources)
    if(NOT _source_count EQUAL 1)
      message(FATAL_ERROR "Expected exactly one manifest source for ${_slot}/${_scenario}")
    endif()
    string(JSON _fingerprint GET "${_json}" sources 0 compile_context_fingerprint)
    if(_scenario STREQUAL "plain")
      set(_plain_fingerprint "${_fingerprint}")
    elseif(NOT _scenario STREQUAL "changed")
      if(NOT _fingerprint STREQUAL _plain_fingerprint)
        message(FATAL_ERROR "The delimiter changed the semantic fingerprint for ${_slot}")
      endif()
    elseif(_fingerprint STREQUAL _plain_fingerprint)
      message(FATAL_ERROR "A real definition change did not change the semantic fingerprint for ${_slot}")
    endif()
  endforeach()
endforeach()

# The builder does not support a literal '--' forced-include filename. Keep
# rejecting it instead of misclassifying its value as an ignorable delimiter.
gentest_fixture_write_file("${_work_dir}/--" "// forced include\n")
gentest_fixture_make_compdb_entry(_value_entry
  DIRECTORY "${_work_dir}" FILE "${_registration}"
  ARGUMENTS "${_clangxx}" ${_driver_flags} -DGENTEST_VARIANT=1 "${_unsigned_char}" ${_force_include} -- -c "${_registration}")
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_value_entry}")
file(REMOVE "${_registration}" "${_manifest}")
execute_process(
  COMMAND "${PROG}" --compdb "${_work_dir}" --host-clang "${_clangxx}"
    --tu-out-dir "${_work_dir}/generated"
    --textual-registration-output "${_registration}"
    --artifact-manifest "${_manifest}" --scan-slot-kind fallback-header "${_header}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _value_rc OUTPUT_VARIABLE _value_out ERROR_VARIABLE _value_err)
if(_value_rc EQUAL 0 OR NOT "${_value_out}${_value_err}" MATCHES "lost semantic compile-context token '--'")
  message(FATAL_ERROR "A lost semantic '--' option value must be rejected:\n${_value_out}\n${_value_err}")
endif()

# Removing the delimiter must not turn a second, option-looking filename into
# a macro definition that the preservation guard then mistakes for equivalent.
gentest_fixture_write_file("${_work_dir}/-DOTHER=1" "// second input\n")
gentest_fixture_make_compdb_entry(_operand_entry
  DIRECTORY "${_work_dir}" FILE "${_registration}"
  ARGUMENTS "${_clangxx}" ${_driver_flags} -DGENTEST_VARIANT=1 -c "${_unsigned_char}" -- "${_registration}" -DOTHER=1)
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_operand_entry}")
file(REMOVE "${_registration}" "${_manifest}")
execute_process(
  COMMAND "${PROG}" --compdb "${_work_dir}" --host-clang "${_clangxx}"
    --tu-out-dir "${_work_dir}/generated"
    --textual-registration-output "${_registration}"
    --artifact-manifest "${_manifest}" --scan-slot-kind fallback-header "${_header}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _operand_rc OUTPUT_VARIABLE _operand_out ERROR_VARIABLE _operand_err)
if(_operand_rc EQUAL 0 OR NOT "${_operand_out}${_operand_err}" MATCHES "lost semantic compile-context token 'operand=-DOTHER=1'")
  message(FATAL_ERROR "An operand becoming an option must be rejected:\n${_operand_out}\n${_operand_err}")
endif()
