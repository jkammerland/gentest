include_guard(GLOBAL)

function(gentest_resolve_test_std out_compile_feature out_codegen_std_arg)
    if(DEFINED GENTEST_TEST_CXX_STANDARD)
        set(_std "${GENTEST_TEST_CXX_STANDARD}")
    else()
        set(_std "23")
    endif()

    if(NOT (_std STREQUAL "20" OR _std STREQUAL "23"))
        message(FATAL_ERROR "GENTEST_TEST_CXX_STANDARD must be 20 or 23 (got '${_std}')")
    endif()

    set(_compile_feature "cxx_std_${_std}")

    set(_codegen_std_arg "-std=c++${_std}")
    if(DEFINED CMAKE_CXX_COMPILER_FRONTEND_VARIANT AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if(_std STREQUAL "20")
            set(_codegen_std_arg "/std:c++20")
        else()
            set(_codegen_std_arg "/std:c++latest")
        endif()
    endif()

    set(${out_compile_feature} "${_compile_feature}" PARENT_SCOPE)
    set(${out_codegen_std_arg} "${_codegen_std_arg}" PARENT_SCOPE)
endfunction()

function(gentest_add_suite suite)
    set(options NO_CTEST)
    set(one_value_args TARGET OUTPUT_DIR CASES)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT GENTEST_TARGET)
        set(GENTEST_TARGET "${PROJECT_NAME}_${suite}_tests")
    endif()

    if(NOT GENTEST_OUTPUT_DIR)
        set(GENTEST_OUTPUT_DIR "${suite}")
    endif()

    if(NOT GENTEST_CASES)
        set(GENTEST_CASES "${CMAKE_CURRENT_SOURCE_DIR}/${suite}/cases.cpp")
    endif()

    add_executable(${GENTEST_TARGET}
        ${GENTEST_CASES})

    if(TARGET gentest_test_suites)
        add_dependencies(gentest_test_suites ${GENTEST_TARGET})
    endif()

    target_link_libraries(${GENTEST_TARGET}
        PRIVATE
            gentest_main)

    gentest_resolve_test_std(_gentest_target_std_feature _gentest_codegen_std_arg)
    target_compile_features(${GENTEST_TARGET} PRIVATE ${_gentest_target_std_feature})
    target_include_directories(${GENTEST_TARGET}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR})

    gentest_attach_codegen(${GENTEST_TARGET}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${GENTEST_OUTPUT_DIR}
        CLANG_ARGS
            ${_gentest_codegen_std_arg}
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR})
    unset(_gentest_codegen_std_arg)
    unset(_gentest_target_std_feature)

    if(NOT GENTEST_NO_CTEST)
        add_test(NAME ${suite} COMMAND ${GENTEST_TARGET})
    endif()
endfunction()

function(gentest_add_manual_regression)
    set(one_value_args TARGET SOURCE)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    if(NOT GENTEST_TARGET OR NOT GENTEST_SOURCE)
        message(FATAL_ERROR "gentest_add_manual_regression: TARGET and SOURCE are required")
    endif()

    add_executable(${GENTEST_TARGET}
        ${GENTEST_SOURCE})
    target_link_libraries(${GENTEST_TARGET} PRIVATE gentest_runtime)
    target_compile_features(${GENTEST_TARGET} PRIVATE cxx_std_20)
    target_include_directories(${GENTEST_TARGET} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

function(gentest_add_cmake_script_test)
    set(options NO_EMULATOR)
    set(one_value_args NAME PROG SCRIPT)
    set(multi_value_args ARGS ENV_VARS DEFINES)
    cmake_parse_arguments(PARSE_ARGV 0 GENTEST "${options}" "${one_value_args}" "${multi_value_args}")

    if(NOT GENTEST_NAME)
        message(FATAL_ERROR "gentest_add_cmake_script_test: NAME is required")
    endif()
    if(NOT GENTEST_PROG)
        message(FATAL_ERROR "gentest_add_cmake_script_test: PROG is required")
    endif()
    if(NOT GENTEST_SCRIPT)
        message(FATAL_ERROR "gentest_add_cmake_script_test: SCRIPT is required")
    endif()

    set(_prog "${GENTEST_PROG}")
    if(TARGET "${GENTEST_PROG}")
        set(_prog "$<TARGET_FILE:${GENTEST_PROG}>")
    endif()
    set(_prog_def "-DPROG:STRING=${_prog}")
    unset(_prog)

    set(_emu_def "")
    if(NOT GENTEST_NO_EMULATOR AND CMAKE_CROSSCOMPILING AND DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND NOT CMAKE_CROSSCOMPILING_EMULATOR STREQUAL "")
        set(_gentest_emu_items ${CMAKE_CROSSCOMPILING_EMULATOR})
        list(LENGTH _gentest_emu_items _gentest_emu_count)
        string(JOIN ";" _gentest_emu_joined ${_gentest_emu_items})
        if(_gentest_emu_count EQUAL 1)
            # Keep the encoded value list-shaped so helper scripts never fall back
            # to shell-style splitting for single paths containing spaces.
            string(APPEND _gentest_emu_joined ";")
        endif()
        string(REPLACE ";" "\\;" _gentest_emu_escaped "${_gentest_emu_joined}")
        set(_emu_def "-DEMU:STRING=${_gentest_emu_escaped}")
        unset(_gentest_emu_count)
        unset(_gentest_emu_items)
        unset(_gentest_emu_escaped)
        unset(_gentest_emu_joined)
    endif()

    set(_args_def "")
    if(GENTEST_ARGS)
        string(JOIN ";" _args_joined ${GENTEST_ARGS})
        string(REPLACE ";" "\\;" _args_escaped "${_args_joined}")
        set(_args_def "-DARGS:STRING=${_args_escaped}")
        unset(_args_escaped)
        unset(_args_joined)
    endif()

    set(_env_def "")
    if(GENTEST_ENV_VARS)
        string(JOIN ";" _env_joined ${GENTEST_ENV_VARS})
        string(REPLACE ";" "\\;" _env_escaped "${_env_joined}")
        set(_env_def "-DENV_VARS:STRING=${_env_escaped}")
        unset(_env_escaped)
        unset(_env_joined)
    endif()

    set(_defines)
    set(_defines_payload "")
    foreach(def IN LISTS GENTEST_DEFINES)
        string(FIND "${def}" ";" _def_semicolon_pos)
        if(_def_semicolon_pos EQUAL -1)
            list(APPEND _defines "-D${def}")
            unset(_def_semicolon_pos)
            continue()
        endif()
        unset(_def_semicolon_pos)

        string(FIND "${def}" "=" _def_eq_pos)
        if(_def_eq_pos EQUAL -1)
            message(FATAL_ERROR "gentest_add_cmake_script_test: DEFINES entry '${def}' must use KEY=VALUE")
        endif()

        string(SUBSTRING "${def}" 0 ${_def_eq_pos} _def_name)
        if(NOT _def_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR "gentest_add_cmake_script_test: invalid define name '${_def_name}' in '${def}'")
        endif()

        string(LENGTH "${def}" _def_len)
        math(EXPR _def_value_pos "${_def_eq_pos} + 1")
        if(_def_value_pos LESS _def_len)
            string(SUBSTRING "${def}" ${_def_value_pos} -1 _def_value)
        else()
            set(_def_value "")
        endif()
        string(HEX "${_def_value}" _def_value_hex)
        if(NOT _defines_payload STREQUAL "")
            string(APPEND _defines_payload "|")
        endif()
        string(APPEND _defines_payload "${_def_name}=${_def_value_hex}")

        unset(_def_eq_pos)
        unset(_def_name)
        unset(_def_len)
        unset(_def_value_pos)
        unset(_def_value)
        unset(_def_value_hex)
    endforeach()
    set(_defines_payload_def "")
    if(NOT _defines_payload STREQUAL "")
        set(_defines_payload_def "-DGENTEST_DEFINES_HEXMAP:STRING=${_defines_payload}")
    endif()
    unset(_defines_payload)

    set(_cmd_args "${_prog_def}")
    if(NOT _args_def STREQUAL "")
        list(APPEND _cmd_args "${_args_def}")
    endif()
    if(NOT _env_def STREQUAL "")
        list(APPEND _cmd_args "${_env_def}")
    endif()
    if(NOT _emu_def STREQUAL "")
        list(APPEND _cmd_args "${_emu_def}")
    endif()
    foreach(_define_arg IN LISTS _defines)
        list(APPEND _cmd_args "${_define_arg}")
    endforeach()
    unset(_define_arg)
    if(NOT _defines_payload_def STREQUAL "")
        list(APPEND _cmd_args "${_defines_payload_def}")
        list(APPEND _cmd_args "-DGENTEST_SCRIPT:FILEPATH=${GENTEST_SCRIPT}" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunScriptTest.cmake")
    else()
        list(APPEND _cmd_args -P "${GENTEST_SCRIPT}")
    endif()

    add_test(NAME ${GENTEST_NAME} COMMAND ${CMAKE_COMMAND} ${_cmd_args})
    set_property(TEST ${GENTEST_NAME} APPEND PROPERTY LABELS "cmake")
    set_property(TEST ${GENTEST_NAME} APPEND PROPERTY SKIP_REGULAR_EXPRESSION "GENTEST_SKIP_TEST:")
endfunction()

function(gentest_add_check_counts)
    set(options LIST)
    set(one_value_args NAME PROG PASS FAIL SKIP XFAIL XPASS CASES EXPECT_RC)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG)
        message(FATAL_ERROR "gentest_add_check_counts: NAME and PROG are required")
    endif()

    set(_defines)
    if(GENTEST_LIST)
        if(GENTEST_CASES STREQUAL "")
            message(FATAL_ERROR "gentest_add_check_counts: LIST requires CASES")
        endif()
        list(APPEND _defines "LIST=ON" "CASES=${GENTEST_CASES}")
    else()
        if(GENTEST_PASS STREQUAL "" OR GENTEST_FAIL STREQUAL "" OR GENTEST_SKIP STREQUAL "")
            message(FATAL_ERROR "gentest_add_check_counts: PASS, FAIL, and SKIP are required")
        endif()
        list(APPEND _defines "PASS=${GENTEST_PASS}" "FAIL=${GENTEST_FAIL}" "SKIP=${GENTEST_SKIP}")
        if(NOT "${GENTEST_XFAIL}" STREQUAL "")
            list(APPEND _defines "XFAIL=${GENTEST_XFAIL}")
        endif()
        if(NOT "${GENTEST_XPASS}" STREQUAL "")
            list(APPEND _defines "XPASS=${GENTEST_XPASS}")
        endif()
        if(NOT "${GENTEST_EXPECT_RC}" STREQUAL "")
            list(APPEND _defines "EXPECT_RC=${GENTEST_EXPECT_RC}")
        endif()
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES ${_defines})
endfunction()

function(gentest_add_check_inventory)
    set(one_value_args NAME PROG CASES PASS FAIL SKIP XFAIL XPASS EXPECT_RC EXPECTED_LIST_FILE)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG)
        message(FATAL_ERROR "gentest_add_check_inventory: NAME and PROG are required")
    endif()
    if(GENTEST_CASES STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_inventory: CASES is required")
    endif()

    set(_defines "CASES=${GENTEST_CASES}")
    if(NOT "${GENTEST_EXPECTED_LIST_FILE}" STREQUAL "")
        list(APPEND _defines "EXPECTED_LIST_FILE=${GENTEST_EXPECTED_LIST_FILE}")
    endif()
    if(NOT "${GENTEST_PASS}" STREQUAL "" OR NOT "${GENTEST_FAIL}" STREQUAL "" OR NOT "${GENTEST_SKIP}" STREQUAL "")
        if(GENTEST_PASS STREQUAL "" OR GENTEST_FAIL STREQUAL "" OR GENTEST_SKIP STREQUAL "")
            message(FATAL_ERROR "gentest_add_check_inventory: PASS, FAIL, and SKIP must all be provided together")
        endif()
        list(APPEND _defines "PASS=${GENTEST_PASS}" "FAIL=${GENTEST_FAIL}" "SKIP=${GENTEST_SKIP}")
        if(NOT "${GENTEST_XFAIL}" STREQUAL "")
            list(APPEND _defines "XFAIL=${GENTEST_XFAIL}")
        endif()
        if(NOT "${GENTEST_XPASS}" STREQUAL "")
            list(APPEND _defines "XPASS=${GENTEST_XPASS}")
        endif()
        if(NOT "${GENTEST_EXPECT_RC}" STREQUAL "")
            list(APPEND _defines "EXPECT_RC=${GENTEST_EXPECT_RC}")
        endif()
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckTestInventory.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES ${_defines})
endfunction()

function(gentest_add_check_exit_code)
    set(options NO_EMULATOR)
    set(one_value_args NAME PROG EXPECT_RC)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR GENTEST_EXPECT_RC STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_exit_code: NAME, PROG, and EXPECT_RC are required")
    endif()

    set(_no_emulator_arg)
    if(GENTEST_NO_EMULATOR)
        set(_no_emulator_arg NO_EMULATOR)
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckExitCode.cmake"
        ${_no_emulator_arg}
        ARGS ${GENTEST_ARGS}
        DEFINES "EXPECT_RC=${GENTEST_EXPECT_RC}")
endfunction()

function(gentest_add_check_contains)
    set(one_value_args NAME PROG EXPECT_SUBSTRING)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR GENTEST_EXPECT_SUBSTRING STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_contains: NAME, PROG, and EXPECT_SUBSTRING are required")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}")
endfunction()

function(gentest_add_check_lines)
    set(one_value_args NAME PROG LINES)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR GENTEST_LINES STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_lines: NAME, PROG, and LINES are required")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES "LINES=${GENTEST_LINES}")
endfunction()

function(gentest_add_check_death)
    set(options NO_EMULATOR)
    set(one_value_args NAME PROG EXPECT_SUBSTRING)
    set(multi_value_args ARGS ENV_VARS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG)
        message(FATAL_ERROR "gentest_add_check_death: NAME and PROG are required")
    endif()

    set(_defines)
    if(NOT GENTEST_EXPECT_SUBSTRING STREQUAL "")
        list(APPEND _defines "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}")
    endif()

    set(_no_emulator_arg "")
    if(GENTEST_NO_EMULATOR)
        set(_no_emulator_arg NO_EMULATOR)
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake"
        ARGS ${GENTEST_ARGS}
        ENV_VARS ${GENTEST_ENV_VARS}
        ${_no_emulator_arg}
        DEFINES ${_defines})
endfunction()

function(gentest_add_check_file_contains)
    set(one_value_args NAME PROG FILE EXPECT_SUBSTRING)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR NOT GENTEST_FILE OR GENTEST_EXPECT_SUBSTRING STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_file_contains: NAME, PROG, FILE, and EXPECT_SUBSTRING are required")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES
            "FILE=${GENTEST_FILE}"
            "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}")
endfunction()

function(gentest_add_run_and_check_file)
    set(one_value_args NAME PROG FILE EXPECT_SUBSTRING EXPECT_RC FORBID_SUBSTRING)
    set(multi_value_args ARGS FORBID_SUBSTRINGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR NOT GENTEST_FILE OR GENTEST_EXPECT_SUBSTRING STREQUAL "")
        message(FATAL_ERROR "gentest_add_run_and_check_file: NAME, PROG, FILE, and EXPECT_SUBSTRING are required")
    endif()

    set(_defines
        "FILE=${GENTEST_FILE}"
        "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}")
    if(NOT GENTEST_EXPECT_RC STREQUAL "")
        list(APPEND _defines "EXPECT_RC=${GENTEST_EXPECT_RC}")
    endif()
    if(NOT GENTEST_FORBID_SUBSTRING STREQUAL "")
        list(APPEND _defines "FORBID_SUBSTRING=${GENTEST_FORBID_SUBSTRING}")
    endif()
    if(GENTEST_FORBID_SUBSTRINGS)
        string(JOIN "|" _forbid_joined ${GENTEST_FORBID_SUBSTRINGS})
        list(APPEND _defines "FORBID_SUBSTRINGS=${_forbid_joined}")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES ${_defines})
endfunction()
