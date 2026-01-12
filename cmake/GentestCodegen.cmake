include_guard(GLOBAL)

if(NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
    set(GENTEST_CODEGEN_EXECUTABLE "" CACHE FILEPATH
        "Path to a host-built gentest_codegen executable used when the in-tree gentest_codegen target is unavailable (e.g. cross-compiling).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_TARGET)
    set(GENTEST_CODEGEN_TARGET "" CACHE STRING
        "CMake target name that produces a runnable gentest_codegen executable (alternative to GENTEST_CODEGEN_EXECUTABLE).")
endif()

function(gentest_attach_codegen target)
    set(options NO_INCLUDE_SOURCES STRICT_FIXTURE QUIET_CLANG)
    set(one_value_args OUTPUT ENTRY)
    set(multi_value_args SOURCES CLANG_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_SOURCES)
        message(FATAL_ERROR "gentest_attach_codegen requires SOURCES")
    endif()

    if(NOT GENTEST_ENTRY)
        set(GENTEST_ENTRY gentest::run_all_tests)
    endif()

    if(NOT GENTEST_OUTPUT)
        set(GENTEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}_generated.cpp")
    endif()

    # Configure-time collision checks: prevent multiple targets (or multiple
    # calls) from writing to the same generated OUTPUT file, which would
    # silently clobber results during the build.
    if("${GENTEST_OUTPUT}" MATCHES "\\$<")
        message(WARNING "gentest_attach_codegen(${target}): OUTPUT contains generator expressions; collision checks skipped: '${GENTEST_OUTPUT}'")
    else()
        set(_gentest_output_path "${GENTEST_OUTPUT}")
        cmake_path(ABSOLUTE_PATH _gentest_output_path BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE
                   OUTPUT_VARIABLE _gentest_output_abs)

        # Use a normalized key for case-insensitive filesystems.
        set(_gentest_output_key "${_gentest_output_abs}")
        if(WIN32)
            string(TOLOWER "${_gentest_output_key}" _gentest_output_key)
        endif()
        string(MD5 _gentest_output_md5 "${_gentest_output_key}")

        get_property(_gentest_prev_owner GLOBAL PROPERTY "GENTEST_CODEGEN_OUTPUT_OWNER_${_gentest_output_md5}")
        if(_gentest_prev_owner)
            if(NOT _gentest_prev_owner STREQUAL "${target}")
                message(FATAL_ERROR
                    "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' is already used by '${_gentest_prev_owner}'. "
                    "Each target must have a unique OUTPUT to avoid generated file clobbering.")
            endif()
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' is registered multiple times for the same target. "
                "Call gentest_attach_codegen() once per target and list all SOURCES in that call.")
        endif()
        set_property(GLOBAL PROPERTY "GENTEST_CODEGEN_OUTPUT_OWNER_${_gentest_output_md5}" "${target}")

        # Also prevent the OUTPUT from overwriting any scanned source file.
        foreach(_gentest_src IN LISTS GENTEST_SOURCES)
            if("${_gentest_src}" MATCHES "\\$<")
                continue()
            endif()
            set(_gentest_src_path "${_gentest_src}")
            cmake_path(ABSOLUTE_PATH _gentest_src_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
                       OUTPUT_VARIABLE _gentest_src_abs)

            set(_gentest_src_key "${_gentest_src_abs}")
            if(WIN32)
                string(TOLOWER "${_gentest_src_key}" _gentest_src_key)
            endif()
            if(_gentest_src_key STREQUAL _gentest_output_key)
                message(FATAL_ERROR
                    "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' would overwrite a scanned source file '${_gentest_src_abs}'.")
            endif()
        endforeach()
    endif()

    set(_gentest_codegen_target "")
    set(_gentest_codegen_executable "")
    if(GENTEST_CODEGEN_EXECUTABLE)
        set(_gentest_codegen_executable "${GENTEST_CODEGEN_EXECUTABLE}")
    elseif(GENTEST_CODEGEN_TARGET)
        if(NOT TARGET ${GENTEST_CODEGEN_TARGET})
            message(FATAL_ERROR "gentest_attach_codegen: GENTEST_CODEGEN_TARGET='${GENTEST_CODEGEN_TARGET}' does not exist")
        endif()
        set(_gentest_codegen_target "${GENTEST_CODEGEN_TARGET}")
        set(_gentest_codegen_executable $<TARGET_FILE:${GENTEST_CODEGEN_TARGET}>)
    elseif(TARGET gentest_codegen)
        set(_gentest_codegen_target gentest_codegen)
        set(_gentest_codegen_executable $<TARGET_FILE:gentest_codegen>)
    else()
        message(FATAL_ERROR
            "gentest_attach_codegen: no gentest code generator available. "
            "Either enable -DGENTEST_BUILD_CODEGEN=ON (native builds) or provide a host tool via "
            "-DGENTEST_CODEGEN_EXECUTABLE=<path> (cross builds).")
    endif()

    get_filename_component(_gentest_output_dir "${GENTEST_OUTPUT}" DIRECTORY)
    if(_gentest_output_dir STREQUAL "")
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    string(MAKE_C_IDENTIFIER "${target}" _gentest_target_id)
    set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
    # Generate inline mock implementations as a header; it will be included
    # by the generated test implementation after including sources.
    set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")

    set(_command_launcher ${_gentest_codegen_executable})
    if(GENTEST_USES_TERMINFO_SHIM AND UNIX AND NOT APPLE AND GENTEST_TERMINFO_SHIM_DIR)
        set(_gentest_ld_library_path "${GENTEST_TERMINFO_SHIM_DIR}")
        if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
            string(APPEND _gentest_ld_library_path ":$ENV{LD_LIBRARY_PATH}")
        endif()
        set(_command_launcher ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_gentest_ld_library_path}"
            ${_gentest_codegen_executable})
    endif()

    set(_command ${_command_launcher}
        --output ${GENTEST_OUTPUT}
        --entry ${GENTEST_ENTRY}
        --mock-registry ${_gentest_mock_registry}
        --mock-impl ${_gentest_mock_impl}
        --compdb ${CMAKE_BINARY_DIR}
        --include-root ${CMAKE_CURRENT_SOURCE_DIR})

    if(GENTEST_NO_INCLUDE_SOURCES)
        list(APPEND _command --no-include-sources)
    endif()
    if(GENTEST_STRICT_FIXTURE)
        list(APPEND _command --strict-fixture)
    endif()
    if(GENTEST_QUIET_CLANG)
        list(APPEND _command --quiet-clang)
    endif()

    list(APPEND _command ${GENTEST_SOURCES})

    list(APPEND _command --)
    list(APPEND _command -DGENTEST_CODEGEN=1)
    if(GENTEST_CLANG_ARGS)
        list(APPEND _command ${GENTEST_CLANG_ARGS})
    endif()

    # Add system include directories from CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    # to ensure gentest_codegen can parse headers correctly with all compilers
    set(_gentest_system_includes "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")
    if(_gentest_system_includes STREQUAL "" AND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        set(_gentest_system_includes "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
    endif()
    if(_gentest_system_includes)
        foreach(_inc_dir ${_gentest_system_includes})
            list(APPEND _command "-isystem" "${_inc_dir}")
        endforeach()
    endif()
    unset(_gentest_system_includes)

    set(_gentest_codegen_deps "")
    if(_gentest_codegen_target)
        list(APPEND _gentest_codegen_deps ${_gentest_codegen_target})
    endif()

    cmake_policy(PUSH)
    if(POLICY CMP0171)
        cmake_policy(SET CMP0171 NEW)
    endif()

    set(_gentest_custom_command_args
        OUTPUT ${GENTEST_OUTPUT} ${_gentest_mock_registry} ${_gentest_mock_impl}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS ${_gentest_codegen_deps} ${GENTEST_SOURCES} ${GENTEST_DEPENDS}
        COMMENT "Generating ${GENTEST_OUTPUT} for target ${target}"
        VERBATIM)
    if(POLICY CMP0171)
        list(APPEND _gentest_custom_command_args CODEGEN)
    endif()
    add_custom_command(${_gentest_custom_command_args})
    unset(_gentest_custom_command_args)

    cmake_policy(POP)

    # Only compile the generated test implementation; the mock impl header
    # is included by it and must not be compiled as a separate TU.
    target_sources(${target} PRIVATE ${GENTEST_OUTPUT})

    get_filename_component(_gentest_mock_dir "${_gentest_mock_registry}" DIRECTORY)
    target_include_directories(${target} PRIVATE ${_gentest_mock_dir})
    # Ensure relative source includes emitted by gentest_codegen are resolvable
    # without relying on absolute paths.
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    get_filename_component(_gentest_mock_header_name "${_gentest_mock_registry}" NAME)
    get_filename_component(_gentest_mock_impl_name "${_gentest_mock_impl}" NAME)
    target_compile_definitions(${target} PRIVATE
        GENTEST_MOCK_REGISTRY_PATH=${_gentest_mock_header_name}
        GENTEST_MOCK_IMPL_PATH=${_gentest_mock_impl_name}
    )
    if(GENTEST_USE_BOOST_JSON)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_JSON)
    endif()
    if(GENTEST_USE_BOOST_UUID)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_UUID)
    endif()
    if(_gentest_codegen_target)
        add_dependencies(${target} ${_gentest_codegen_target})
    endif()
endfunction()

function(_gentest_write_discover_tests_script out_script)
    set(_gentest_script_dir "${CMAKE_BINARY_DIR}/gentest")
    file(MAKE_DIRECTORY "${_gentest_script_dir}")

    set(_gentest_add_tests_script "${_gentest_script_dir}/GentestAddTests.cmake")
    file(WRITE "${_gentest_add_tests_script}" [====[
cmake_minimum_required(VERSION 3.31)

function(_gentest_add_command name test_name)
  set(args "")
  foreach(arg ${ARGN})
    if(arg MATCHES "[^-./:a-zA-Z0-9_]")
      string(APPEND args " [==[${arg}]==]")
    else()
      string(APPEND args " ${arg}")
    endif()
  endforeach()
  string(APPEND script "${name}(${test_name} ${args})\n")
  set(script "${script}" PARENT_SCOPE)
endfunction()

function(_gentest_generate_testname_guards output open_guard_var close_guard_var)
  set(open_guard "[=[")
  set(close_guard "]=]")
  set(counter 1)
  while("${output}" MATCHES "${close_guard}")
    math(EXPR counter "${counter} + 1")
    string(REPEAT "=" ${counter} equals)
    set(open_guard "[${equals}[")
    set(close_guard "]${equals}]")
  endwhile()
  set(${open_guard_var} "${open_guard}" PARENT_SCOPE)
  set(${close_guard_var} "${close_guard}" PARENT_SCOPE)
endfunction()

function(_gentest_escape_square_brackets output bracket placeholder placeholder_var output_var)
  if("${output}" MATCHES "\\${bracket}")
    set(placeholder "${placeholder}")
    while("${output}" MATCHES "${placeholder}")
      set(placeholder "${placeholder}_")
    endwhile()
    string(REPLACE "${bracket}" "${placeholder}" output "${output}")
    set(${placeholder_var} "${placeholder}" PARENT_SCOPE)
    set(${output_var} "${output}" PARENT_SCOPE)
  endif()
endfunction()

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
  )
  set(multiValueArgs "")
  cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

  set(prefix "${arg_TEST_PREFIX}")
  set(suffix "${arg_TEST_SUFFIX}")
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
      COMMAND ${launcher_args} [==[${arg_TEST_EXECUTABLE}]==] --list-tests ${discovery_extra_args}
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

  _gentest_generate_testname_guards("${output}" open_guard close_guard)
  _gentest_escape_square_brackets("${output}" "[" "__osb" open_sb output)
  _gentest_escape_square_brackets("${output}" "]" "__csb" close_sb output)
  string(REPLACE [[;]] [[\;]] output "${output}")
  string(REPLACE "\r\n" "\n" output "${output}")
  string(REPLACE "\n" ";" output "${output}")

  foreach(line IN LISTS output)
    string(STRIP "${line}" case_name_raw)
    if(case_name_raw STREQUAL "")
      continue()
    endif()

    set(case_id "${case_name_raw}")
    if(open_sb)
      string(REPLACE "${open_sb}" "[" case_id "${case_id}")
    endif()
    if(close_sb)
      string(REPLACE "${close_sb}" "]" case_id "${case_id}")
    endif()

    if(filter_regex AND NOT case_id MATCHES "${filter_regex}")
      continue()
    endif()

    set(testname "${prefix}${case_id}${suffix}")

    set(guarded_testname "${open_guard}${testname}${close_guard}")

    # Preserve empty arguments in TEST_EXECUTOR and EXTRA_ARGS by forwarding them as a bracket-quoted list.
    string(APPEND script "add_test(${guarded_testname} ${launcher_args}")
    foreach(arg IN ITEMS
      "${arg_TEST_EXECUTABLE}"
      "--run-test=${case_id}"
      )
      if(arg MATCHES "[^-./:a-zA-Z0-9_]")
        string(APPEND script " [==[${arg}]==]")
      else()
        string(APPEND script " ${arg}")
      endif()
    endforeach()
    if(arg_TEST_EXTRA_ARGS)
      list(JOIN arg_TEST_EXTRA_ARGS "]==] [==[" extra_args)
      string(APPEND script " [==[${extra_args}]==]")
    endif()
    string(APPEND script ")\n")

    _gentest_add_command(set_tests_properties
      "${guarded_testname}"
      PROPERTIES
      WORKING_DIRECTORY "${arg_TEST_WORKING_DIR}"
      SKIP_REGULAR_EXPRESSION "\\[ SKIP \\]"
      ${arg_TEST_PROPERTIES}
    )

    string(REPLACE [[;]] [[\\;]] _testname_escaped "${testname}")
    list(APPEND tests "${_testname_escaped}")

    string(LENGTH "${script}" script_len)
    if(script_len GREATER "50000")
      file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
      set(file_write_mode APPEND)
      set(script "")
    endif()
  endforeach()

  _gentest_add_command(set "" ${arg_TEST_LIST} "${tests}")

  file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  gentest_discover_tests_impl(
    TEST_EXECUTABLE ${TEST_EXECUTABLE}
    TEST_EXECUTOR "${TEST_EXECUTOR}"
    TEST_WORKING_DIR ${TEST_WORKING_DIR}
    TEST_PREFIX ${TEST_PREFIX}
    TEST_SUFFIX ${TEST_SUFFIX}
    TEST_FILTER ${TEST_FILTER}
    TEST_LIST ${TEST_LIST}
    CTEST_FILE ${CTEST_FILE}
    TEST_DISCOVERY_TIMEOUT ${TEST_DISCOVERY_TIMEOUT}
    TEST_EXTRA_ARGS "${TEST_EXTRA_ARGS}"
    TEST_DISCOVERY_EXTRA_ARGS "${TEST_DISCOVERY_EXTRA_ARGS}"
    TEST_PROPERTIES "${TEST_PROPERTIES}"
  )
endif()
]====])

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
        DISCOVERY_MODE)
    set(multi_value_args EXTRA_ARGS DISCOVERY_EXTRA_ARGS PROPERTIES)
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

    set(_gentest_ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${target}[${_gentest_counter}]")
    set(_gentest_ctest_include_file "${_gentest_ctest_file_base}_include.cmake")
    set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests.cmake")

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
                -P "${_gentest_add_tests_script}"
            VERBATIM
        )

        file(WRITE "${_gentest_ctest_include_file}"
            "if(EXISTS \"${_gentest_ctest_tests_file}\")\n"
            "  include(\"${_gentest_ctest_tests_file}\")\n"
            "else()\n"
            "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)\n"
            "endif()\n"
        )
    elseif(GENTEST_DISCOVERY_MODE STREQUAL "PRE_TEST")
        get_property(_gentest_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(_gentest_is_multi_config)
            set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests-$<CONFIG>.cmake")
        endif()

        string(CONCAT _gentest_ctest_include_content
            "if(EXISTS \"$<TARGET_FILE:${target}>\")" "\n"
            "  if(NOT EXISTS \"${_gentest_ctest_tests_file}\" OR" "\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"$<TARGET_FILE:${target}>\" OR\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"\${CMAKE_CURRENT_LIST_FILE}\")\n"
            "    include([==[${_gentest_add_tests_script}]==])" "\n"
            "    gentest_discover_tests_impl(" "\n"
            "      TEST_EXECUTABLE [==[$<TARGET_FILE:${target}>]==]" "\n"
            "      TEST_EXECUTOR [==[${_gentest_test_executor}]==]" "\n"
            "      TEST_WORKING_DIR [==[${GENTEST_WORKING_DIRECTORY}]==]" "\n"
            "      TEST_EXTRA_ARGS [==[${GENTEST_EXTRA_ARGS}]==]" "\n"
            "      TEST_PROPERTIES [==[${GENTEST_PROPERTIES}]==]" "\n"
            "      TEST_PREFIX [==[${GENTEST_TEST_PREFIX}]==]" "\n"
            "      TEST_SUFFIX [==[${GENTEST_TEST_SUFFIX}]==]" "\n"
            "      TEST_FILTER [==[${GENTEST_TEST_FILTER}]==]" "\n"
            "      TEST_LIST [==[${GENTEST_TEST_LIST}]==]" "\n"
            "      CTEST_FILE [==[${_gentest_ctest_tests_file}]==]" "\n"
            "      TEST_DISCOVERY_TIMEOUT [==[${GENTEST_DISCOVERY_TIMEOUT}]==]" "\n"
            "      TEST_DISCOVERY_EXTRA_ARGS [==[${GENTEST_DISCOVERY_EXTRA_ARGS}]==]" "\n"
            "    )" "\n"
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
