include_guard(GLOBAL)

function(_gentest_append_args)
  # ARGV indices retain semicolons and unmatched brackets in individual names.
  set(index 0)
  while(index LESS ARGC)
    set(value "${ARGV${index}}")
    # Keep the original guard width for paths expanded from generator expressions later.
    set(equals "==")
    # A closing-delimiter prefix at the value's end can overlap our appended ']'.
    string(FIND "${value}" "]${equals}" close_pos)
    while(NOT close_pos EQUAL -1)
      string(APPEND equals "=")
      string(FIND "${value}" "]${equals}" close_pos)
    endwhile()
    string(APPEND script " [${equals}[\n${value}]${equals}]")
    math(EXPR index "${index} + 1")
  endwhile()
  set(script "${script}" PARENT_SCOPE)
endfunction()

function(_gentest_add_command name)
  string(APPEND script "${name}(")
  set(index 1)
  while(index LESS ARGC)
    _gentest_append_args("${ARGV${index}}")
    math(EXPR index "${index} + 1")
  endwhile()
  string(APPEND script ")\n")
  set(script "${script}" PARENT_SCOPE)
endfunction()

function(_gentest_write_discover_tests_script out_script)
    set(_gentest_script_dir "${CMAKE_BINARY_DIR}/gentest")
    file(MAKE_DIRECTORY "${_gentest_script_dir}")

    set(_gentest_add_tests_script "${_gentest_script_dir}/GentestAddTests.cmake")
    set(script)
    _gentest_add_command(include "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    set(_gentest_helper_include "${script}")
    set(_gentest_script_content [====[
cmake_minimum_required(VERSION 3.31)

set(_gentest_cmake_command "@CMAKE_COMMAND@")

@_gentest_helper_include@

function(_gentest_wildcard_to_regex out_var pat)
  # Convert a simple wildcard (*, ?) pattern to an anchored CMake regex.
  set(_s "${pat}")
  string(REPLACE "\\" "\\\\" _s "${_s}")
  string(REPLACE "." "\\." _s "${_s}")
  string(REPLACE "+" "\\+" _s "${_s}")
  string(REPLACE "(" "\\(" _s "${_s}")
  string(REPLACE ")" "\\)" _s "${_s}")
  string(REPLACE "[" "\\[" _s "${_s}")
  string(REPLACE "]" "\\]" _s "${_s}")
  string(REPLACE "{" "\\{" _s "${_s}")
  string(REPLACE "}" "\\}" _s "${_s}")
  string(REPLACE "^" "\\^" _s "${_s}")
  string(REPLACE "$" "\\$" _s "${_s}")
  string(REPLACE "|" "\\|" _s "${_s}")
  string(REPLACE "*" ".*" _s "${_s}")
  string(REPLACE "?" "." _s "${_s}")
  set(${out_var} "^${_s}$" PARENT_SCOPE)
endfunction()

function(_gentest_ensure_check_death_script out_var)
  set(_script_dir "${CMAKE_CURRENT_LIST_DIR}")
  file(MAKE_DIRECTORY "${_script_dir}")
  set(_script "${_script_dir}/GentestCheckDeath.cmake")
  file(WRITE "${_script}" [==[
# Requires:
#  -DPROG=<path to test binary>
#  -DCASE_ID=<case name, preserved as one argument>
#  -DEXTRA_ARGS=<optional CMake list of extra CLI args>
#  -DARGS=<legacy optional CLI args, used without CASE_ID>
#  -DENV_VARS=<optional env vars (list of KEY=VALUE)>
#  -DDEATH_EXPECT_SUBSTRING=<substring expected in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckDeath.cmake: PROG not set")
endif()
if(NOT EXISTS "${PROG}" OR IS_DIRECTORY "${PROG}")
  message(FATAL_ERROR "Death test process could not execute: executable does not exist: ${PROG}")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU}) # already a list
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}") # string
  endif()
endif()

set(_args)
if(DEFINED EXTRA_ARGS)
  set(_args "${EXTRA_ARGS}")
elseif(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

if(DEFINED ENV_VARS)
  foreach(kv IN LISTS ENV_VARS)
    string(FIND "${kv}" "=" _equals)
    if(_equals LESS 1)
      message(FATAL_ERROR "Invalid death test environment assignment: ${kv}")
    endif()
    string(SUBSTRING "${kv}" 0 ${_equals} _key)
    math(EXPR _value_start "${_equals} + 1")
    string(SUBSTRING "${kv}" ${_value_start} -1 _value)
    set(ENV{${_key}} "${_value}")
  endforeach()
endif()

if(DEFINED CASE_ID)
  execute_process(
    COMMAND ${_emu} "${PROG}" --include-death "--run=${CASE_ID}" ${_args}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
else()
  execute_process(
    COMMAND ${_emu} "${PROG}" ${_args}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
endif()

set(_all "${_out}\n${_err}")

# execute_process returns text for both fatal signals and launch errors.
# Accept CMake's termination descriptions; other text means execution failed.
# See cmUVProcessChain::Status::GetException in CMake.
set(_death_exception_pattern
  "^(Segmentation fault|Bus error|Floating-point .+|Illegal instruction|User interrupt|Subprocess (aborted|killed|terminated)|SIG[A-Z0-9]+|Signal [0-9]+|Divide-by-zero|Invalid floating-point operation|Integer (divide-by-zero|overflow)|Datatype misalignment|Access violation|In-page error|Invalid handle|Noncontinuable exception|Invalid disposition|Array bounds exceeded|Stack overflow|Privileged instruction|Exit code 0x[0-9a-fA-F]+[\n]?)$")
if(NOT _rc MATCHES "^-?[0-9]+$" AND NOT _rc MATCHES "${_death_exception_pattern}")
  message(FATAL_ERROR "Death test process could not execute: ${_rc}. Output:\n${_all}")
endif()

set(_missing_case "${CASE_ID}")
if(NOT DEFINED CASE_ID)
  foreach(_arg IN LISTS _args)
    if(_arg MATCHES "^--run=(.+)$")
      set(_missing_case "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
endif()

set(_missing_case_line FALSE)
if(NOT _missing_case STREQUAL "")
  string(REPLACE "\r\n" "\n" _all_norm "\n${_all}\n")
  foreach(_prefix IN ITEMS "Case not found: " "Test not found: ")
    string(FIND "${_all_norm}" "\n${_prefix}${_missing_case}\n" _missing_pos)
    if(NOT _missing_pos EQUAL -1)
      set(_missing_case_line TRUE)
    endif()
  endforeach()
endif()

# Some older runners use a generic non-zero exit code for absent cases.
if(_missing_case_line)
  message(STATUS "[ SKIP ] Death test not present in this build configuration")
  return()
endif()

if(_all MATCHES "(^|\n)\\[ SKIP \\]")
  message(STATUS "[ SKIP ] Death test skipped by test binary")
  return()
endif()

if(_all MATCHES "tagged as a death test" OR _all MATCHES "death tests excluded")
  message(FATAL_ERROR "Death test did not run with --include-death. Output:\n${_all}")
endif()

if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected process to abort/exit non-zero, but exit code was 0. Output:\n${_all}")
endif()

if(_all MATCHES "(^|\n)\\[ FAIL \\]")
  message(FATAL_ERROR "Death test exited non-zero but reported a normal test failure. Output:\n${_all}")
endif()

if(DEFINED DEATH_EXPECT_SUBSTRING)
  string(FIND "${_all}" "${DEATH_EXPECT_SUBSTRING}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in output: '${DEATH_EXPECT_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

message(STATUS "Death test passed (non-zero exit and expected output present)")
]==])
  set(${out_var} "${_script}" PARENT_SCOPE)
endfunction()

function(gentest_discover_tests_impl)
  set(options "")
  set(oneValueArgs
    TEST_EXECUTABLE
    TEST_WORKING_DIR
    TEST_PREFIX
    TEST_SUFFIX
    TEST_FILTER
    TEST_LIST
    CTEST_FILE
    TEST_DISCOVERY_TIMEOUT
    # The following are all multi-value arguments in gentest_discover_tests(),
    # but are each passed as a single argument to preserve escaping.
    TEST_EXTRA_ARGS
    TEST_DISCOVERY_EXTRA_ARGS
    TEST_PROPERTIES
    TEST_EXECUTOR
    DEATH_EXPECT_SUBSTRING
  )
  set(multiValueArgs "")
  cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

  set(prefix "${arg_TEST_PREFIX}")
  set(suffix "${arg_TEST_SUFFIX}")
  set(death_prefix "death/")
  set(death_suffix "")
  set(script)
  set(tests)
  set(file_write_mode WRITE)

  if(NOT EXISTS "${arg_TEST_EXECUTABLE}")
    message(FATAL_ERROR "Specified test executable does not exist: '${arg_TEST_EXECUTABLE}'")
  endif()

  set(launcher_args "")
  if(NOT "${arg_TEST_EXECUTOR}" STREQUAL "")
    list(JOIN arg_TEST_EXECUTOR "]==] [==[" launcher_args)
    set(launcher_args "[==[${launcher_args}]==]")
  endif()

  set(discovery_extra_args "")
  if(NOT "${arg_TEST_DISCOVERY_EXTRA_ARGS}" STREQUAL "")
    list(JOIN arg_TEST_DISCOVERY_EXTRA_ARGS "]==] [==[" discovery_extra_args)
    set(discovery_extra_args "[==[${discovery_extra_args}]==]")
  endif()

  if("${arg_TEST_DISCOVERY_TIMEOUT}" STREQUAL "")
    set(arg_TEST_DISCOVERY_TIMEOUT 5)
  endif()

  cmake_language(EVAL CODE
    "execute_process(
      COMMAND ${launcher_args} [==[${arg_TEST_EXECUTABLE}]==] --list-json ${discovery_extra_args}
      WORKING_DIRECTORY [==[${arg_TEST_WORKING_DIR}]==]
      TIMEOUT ${arg_TEST_DISCOVERY_TIMEOUT}
      OUTPUT_VARIABLE output
      ERROR_VARIABLE error_output
      RESULT_VARIABLE result
    )"
  )
  if(NOT result EQUAL 0)
    string(REPLACE "\n" "\n    " output "${output}")
    string(REPLACE "\n" "\n    " error_output "${error_output}")
    if(arg_TEST_EXECUTOR)
      set(path "${arg_TEST_EXECUTOR} ${arg_TEST_EXECUTABLE}")
    else()
      set(path "${arg_TEST_EXECUTABLE}")
    endif()
    message(FATAL_ERROR
      "Error running test executable.\n"
      "  Path: '${path}'\n"
      "  Working directory: '${arg_TEST_WORKING_DIR}'\n"
      "  Result: ${result}\n"
      "  Command: --list-json\n"
      "  Stdout:\n"
      "    ${output}\n"
      "  Stderr:\n"
      "    ${error_output}\n"
    )
  endif()

  set(filter_regex "")
  if(arg_TEST_FILTER)
    _gentest_wildcard_to_regex(filter_regex "${arg_TEST_FILTER}")
  endif()

  string(JSON case_count LENGTH "${output}")
  set(case_index 0)
  while(case_index LESS case_count)
    string(JSON case_id GET "${output}" ${case_index} name)
    string(JSON skipped GET "${output}" ${case_index} skipped)
    string(JSON tag_count LENGTH "${output}" ${case_index} tags)
    set(is_death FALSE)
    set(tag_index 0)
    while(tag_index LESS tag_count)
      string(JSON tag GET "${output}" ${case_index} tags ${tag_index})
      string(TOLOWER "${tag}" tag)
      if(tag STREQUAL "death")
        set(is_death TRUE)
      endif()
      math(EXPR tag_index "${tag_index} + 1")
    endwhile()
    math(EXPR case_index "${case_index} + 1")
    if((is_death AND skipped) OR (filter_regex AND NOT case_id MATCHES "${filter_regex}"))
      continue()
    endif()

    if(is_death)
      if(NOT DEFINED _gentest_check_death_script)
        _gentest_ensure_check_death_script(_gentest_check_death_script)
      endif()
      set(testname "${prefix}${death_prefix}${case_id}${death_suffix}${suffix}")
      _gentest_add_command(add_test "${testname}" "${_gentest_cmake_command}"
        "-DPROG=${arg_TEST_EXECUTABLE}" "-DCASE_ID=${case_id}"
        "-DEXTRA_ARGS=${arg_TEST_EXTRA_ARGS}" "-DEMU=${arg_TEST_EXECUTOR}"
        "-DDEATH_EXPECT_SUBSTRING=${arg_DEATH_EXPECT_SUBSTRING}"
        "-P" "${_gentest_check_death_script}")
    else()
      set(testname "${prefix}${case_id}${suffix}")
      string(APPEND script "add_test(")
      _gentest_append_args("${testname}")
      foreach(arg IN LISTS arg_TEST_EXECUTOR)
        _gentest_append_args("${arg}")
      endforeach()
      _gentest_append_args("${arg_TEST_EXECUTABLE}" "--run=${case_id}")
      foreach(arg IN LISTS arg_TEST_EXTRA_ARGS)
        _gentest_append_args("${arg}")
      endforeach()
      string(APPEND script ")\n")
    endif()

    string(APPEND script "set_tests_properties(")
    _gentest_append_args("${testname}" PROPERTIES WORKING_DIRECTORY "${arg_TEST_WORKING_DIR}")
    if(is_death)
      _gentest_append_args(SKIP_REGULAR_EXPRESSION "\\[ SKIP \\]")
    endif()
    foreach(arg IN LISTS arg_TEST_PROPERTIES)
      _gentest_append_args("${arg}")
    endforeach()
    string(APPEND script ")\n")

    # CMake lists cannot represent unmatched brackets or trailing backslashes.
    # These cases are still registered and can be selected by CTest directly.
    string(FIND "${testname}" "[" open_bracket)
    string(FIND "${testname}" "]" close_bracket)
    if(open_bracket EQUAL -1 AND close_bracket EQUAL -1 AND NOT testname MATCHES "\\\\$")
      string(REPLACE [[;]] [[\;]] _testname_escaped "${testname}")
      list(APPEND tests "${_testname_escaped}")
    endif()
    string(LENGTH "${script}" script_len)
    if(script_len GREATER "50000")
      file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
      set(file_write_mode APPEND)
      set(script "")
    endif()
  endwhile()
  _gentest_add_command(set "${arg_TEST_LIST}" "${tests}")

  file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  gentest_discover_tests_impl(
    TEST_EXECUTABLE "${TEST_EXECUTABLE}"
    TEST_EXECUTOR "${TEST_EXECUTOR}"
    TEST_WORKING_DIR "${TEST_WORKING_DIR}"
    TEST_PREFIX "${TEST_PREFIX}"
    TEST_SUFFIX "${TEST_SUFFIX}"
    TEST_FILTER "${TEST_FILTER}"
    TEST_LIST "${TEST_LIST}"
    CTEST_FILE "${CTEST_FILE}"
    TEST_DISCOVERY_TIMEOUT "${TEST_DISCOVERY_TIMEOUT}"
    TEST_EXTRA_ARGS "${TEST_EXTRA_ARGS}"
    TEST_DISCOVERY_EXTRA_ARGS "${TEST_DISCOVERY_EXTRA_ARGS}"
    TEST_PROPERTIES "${TEST_PROPERTIES}"
    DEATH_EXPECT_SUBSTRING "${DEATH_EXPECT_SUBSTRING}"
  )
endif()
]====])
    string(CONFIGURE "${_gentest_script_content}" _gentest_script_content @ONLY)
    file(WRITE "${_gentest_add_tests_script}" "${_gentest_script_content}")

    set(${out_script} "${_gentest_add_tests_script}" PARENT_SCOPE)
endfunction()

function(gentest_discover_tests target)
    set(options "")
    set(one_value_args
        TEST_PREFIX
        TEST_SUFFIX
        TEST_FILTER
        WORKING_DIRECTORY
        TEST_LIST
        DISCOVERY_TIMEOUT
        DISCOVERY_MODE
        DEATH_EXPECT_SUBSTRING)
    set(multi_value_args EXTRA_ARGS DISCOVERY_EXTRA_ARGS PROPERTIES)
    set(_gentest_discover_keyword_args ${one_value_args} ${multi_value_args})
    set(_gentest_in_multi_value_arg FALSE)
    list(LENGTH ARGN _gentest_arg_count)
    set(_gentest_arg_idx 0)
    while(_gentest_arg_idx LESS _gentest_arg_count)
        list(GET ARGN ${_gentest_arg_idx} _gentest_arg)

        if(_gentest_in_multi_value_arg)
            list(FIND _gentest_discover_keyword_args "${_gentest_arg}" _gentest_known_arg_idx)
            if(_gentest_known_arg_idx EQUAL -1)
                math(EXPR _gentest_arg_idx "${_gentest_arg_idx} + 1")
                continue()
            endif()
            set(_gentest_in_multi_value_arg FALSE)
        endif()

        if(_gentest_arg STREQUAL "EXPECT_SUBSTRING")
            message(FATAL_ERROR
                "gentest_discover_tests: EXPECT_SUBSTRING was removed in gentest 2.0.0; use DEATH_EXPECT_SUBSTRING instead.")
        endif()
        list(FIND multi_value_args "${_gentest_arg}" _gentest_multi_arg_idx)
        if(NOT _gentest_multi_arg_idx EQUAL -1)
            set(_gentest_in_multi_value_arg TRUE)
            math(EXPR _gentest_arg_idx "${_gentest_arg_idx} + 1")
            continue()
        endif()
        list(FIND one_value_args "${_gentest_arg}" _gentest_one_arg_idx)
        if(NOT _gentest_one_arg_idx EQUAL -1)
            math(EXPR _gentest_arg_idx "${_gentest_arg_idx} + 2")
            continue()
        endif()
        math(EXPR _gentest_arg_idx "${_gentest_arg_idx} + 1")
    endwhile()

    cmake_parse_arguments(PARSE_ARGV 1 GENTEST "${options}" "${one_value_args}" "${multi_value_args}")

    if(NOT TARGET ${target})
        message(FATAL_ERROR "gentest_discover_tests: target '${target}' does not exist")
    endif()

    get_target_property(_gentest_target_type ${target} TYPE)
    if(NOT _gentest_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "gentest_discover_tests: target '${target}' must be an executable")
    endif()

    if(NOT GENTEST_WORKING_DIRECTORY)
        set(GENTEST_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(NOT GENTEST_TEST_LIST)
        set(GENTEST_TEST_LIST ${target}_TESTS)
    endif()
    if(NOT GENTEST_DISCOVERY_TIMEOUT)
        set(GENTEST_DISCOVERY_TIMEOUT 5)
    endif()
    if(NOT GENTEST_DISCOVERY_MODE)
        if(NOT CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE)
            set(CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE "POST_BUILD")
        endif()
        set(GENTEST_DISCOVERY_MODE ${CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE})
    endif()
    get_property(_gentest_has_counter TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER SET)
    if(_gentest_has_counter)
        get_property(_gentest_counter TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER)
        math(EXPR _gentest_counter "${_gentest_counter} + 1")
    else()
        set(_gentest_counter 1)
    endif()
    set_property(TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER ${_gentest_counter})

    get_property(_gentest_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(_gentest_ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${target}[${_gentest_counter}]")
    set(_gentest_ctest_include_file "${_gentest_ctest_file_base}_include.cmake")
    if(_gentest_is_multi_config)
        set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests-$<CONFIG>.cmake")
    else()
        set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests.cmake")
    endif()

    get_property(_gentest_test_launcher TARGET ${target} PROPERTY TEST_LAUNCHER)
    get_property(_gentest_crosscompiling_emulator TARGET ${target} PROPERTY CROSSCOMPILING_EMULATOR)
    if(_gentest_test_launcher AND _gentest_crosscompiling_emulator)
        set(_gentest_test_executor "${_gentest_test_launcher}" "${_gentest_crosscompiling_emulator}")
    elseif(_gentest_test_launcher)
        set(_gentest_test_executor "${_gentest_test_launcher}")
    elseif(_gentest_crosscompiling_emulator)
        set(_gentest_test_executor "${_gentest_crosscompiling_emulator}")
    else()
        set(_gentest_test_executor "")
    endif()

    _gentest_write_discover_tests_script(_gentest_add_tests_script)

    if(GENTEST_DISCOVERY_MODE STREQUAL "POST_BUILD")
        add_custom_command(
            TARGET ${target} POST_BUILD
            BYPRODUCTS "${_gentest_ctest_tests_file}"
            COMMAND "${CMAKE_COMMAND}"
                -D "TEST_EXECUTABLE=$<TARGET_FILE:${target}>"
                -D "TEST_EXECUTOR=${_gentest_test_executor}"
                -D "TEST_WORKING_DIR=${GENTEST_WORKING_DIRECTORY}"
                -D "TEST_EXTRA_ARGS=${GENTEST_EXTRA_ARGS}"
                -D "TEST_PROPERTIES=${GENTEST_PROPERTIES}"
                -D "TEST_PREFIX=${GENTEST_TEST_PREFIX}"
                -D "TEST_SUFFIX=${GENTEST_TEST_SUFFIX}"
                -D "TEST_FILTER=${GENTEST_TEST_FILTER}"
                -D "TEST_LIST=${GENTEST_TEST_LIST}"
                -D "CTEST_FILE=${_gentest_ctest_tests_file}"
                -D "TEST_DISCOVERY_TIMEOUT=${GENTEST_DISCOVERY_TIMEOUT}"
                -D "TEST_DISCOVERY_EXTRA_ARGS=${GENTEST_DISCOVERY_EXTRA_ARGS}"
                -D "DEATH_EXPECT_SUBSTRING=${GENTEST_DEATH_EXPECT_SUBSTRING}"
                -P "${_gentest_add_tests_script}"
            VERBATIM
        )

        if(_gentest_is_multi_config)
            file(WRITE "${_gentest_ctest_include_file}"
                "set(_gentest_config_tests_file \"${_gentest_ctest_file_base}_tests-\${CTEST_CONFIGURATION_TYPE}.cmake\")\n"
                "if(EXISTS \"\${_gentest_config_tests_file}\")\n"
                "  include(\"\${_gentest_config_tests_file}\")\n"
                "else()\n"
                "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)\n"
                "endif()\n"
                "unset(_gentest_config_tests_file)\n")
        else()
            file(WRITE "${_gentest_ctest_include_file}"
                "if(EXISTS \"${_gentest_ctest_tests_file}\")\n"
                "  include(\"${_gentest_ctest_tests_file}\")\n"
                "else()\n"
                "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)\n"
                "endif()\n")
        endif()
    elseif(GENTEST_DISCOVERY_MODE STREQUAL "PRE_TEST")
        set(script)
        _gentest_add_command(gentest_discover_tests_impl
            TEST_EXECUTABLE "$<TARGET_FILE:${target}>"
            TEST_EXECUTOR "${_gentest_test_executor}"
            TEST_WORKING_DIR "${GENTEST_WORKING_DIRECTORY}"
            TEST_EXTRA_ARGS "${GENTEST_EXTRA_ARGS}"
            TEST_PROPERTIES "${GENTEST_PROPERTIES}"
            TEST_PREFIX "${GENTEST_TEST_PREFIX}"
            TEST_SUFFIX "${GENTEST_TEST_SUFFIX}"
            TEST_FILTER "${GENTEST_TEST_FILTER}"
            TEST_LIST "${GENTEST_TEST_LIST}"
            CTEST_FILE "${_gentest_ctest_tests_file}"
            TEST_DISCOVERY_TIMEOUT "${GENTEST_DISCOVERY_TIMEOUT}"
            TEST_DISCOVERY_EXTRA_ARGS "${GENTEST_DISCOVERY_EXTRA_ARGS}"
            DEATH_EXPECT_SUBSTRING "${GENTEST_DEATH_EXPECT_SUBSTRING}")
        string(CONCAT _gentest_ctest_include_content
            "if(EXISTS \"$<TARGET_FILE:${target}>\")" "\n"
            "  if(NOT EXISTS \"${_gentest_ctest_tests_file}\" OR" "\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"$<TARGET_FILE:${target}>\" OR\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"\${CMAKE_CURRENT_LIST_FILE}\")\n"
            "    include([==[${_gentest_add_tests_script}]==])" "\n"
            "${script}"
            "  endif()" "\n"
            "  include(\"${_gentest_ctest_tests_file}\")" "\n"
            "else()" "\n"
            "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)" "\n"
            "endif()" "\n"
        )

        if(_gentest_is_multi_config)
            foreach(_gentest_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
                file(GENERATE
                    OUTPUT "${_gentest_ctest_file_base}_include-${_gentest_cfg}.cmake"
                    CONTENT "${_gentest_ctest_include_content}"
                    CONDITION $<CONFIG:${_gentest_cfg}>
                )
            endforeach()
            file(WRITE "${_gentest_ctest_include_file}"
                "include(\"${_gentest_ctest_file_base}_include-\${CTEST_CONFIGURATION_TYPE}.cmake\")"
            )
        else()
            file(GENERATE
                OUTPUT "${_gentest_ctest_file_base}_include.cmake"
                CONTENT "${_gentest_ctest_include_content}"
            )
            file(WRITE "${_gentest_ctest_include_file}"
                "include(\"${_gentest_ctest_file_base}_include.cmake\")"
            )
        endif()
    else()
        message(FATAL_ERROR "gentest_discover_tests: unknown DISCOVERY_MODE '${GENTEST_DISCOVERY_MODE}'")
    endif()

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_gentest_ctest_include_file}")
endfunction()
