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
    set(GENTEST_NO_EMULATOR FALSE)
    set(GENTEST_NAME "")
    set(GENTEST_PROG "")
    set(GENTEST_SCRIPT "")
    set(GENTEST_ARGS)
    set(GENTEST_ENV_VARS)
    set(GENTEST_DEFINES)

    set(_gentest_current_multi "")
    set(_gentest_index 0)
    while(_gentest_index LESS ARGC)
        set(_gentest_arg "${ARGV${_gentest_index}}")

        if(_gentest_arg STREQUAL "NO_EMULATOR")
            set(GENTEST_NO_EMULATOR TRUE)
            set(_gentest_current_multi "")
        elseif(_gentest_arg STREQUAL "NAME" OR _gentest_arg STREQUAL "PROG" OR _gentest_arg STREQUAL "SCRIPT")
            math(EXPR _gentest_index "${_gentest_index} + 1")
            if(_gentest_index GREATER_EQUAL ARGC)
                message(FATAL_ERROR "gentest_add_cmake_script_test: ${_gentest_arg} requires a value")
            endif()
            set(_gentest_value "${ARGV${_gentest_index}}")
            if(_gentest_arg STREQUAL "NAME")
                set(GENTEST_NAME "${_gentest_value}")
            elseif(_gentest_arg STREQUAL "PROG")
                set(GENTEST_PROG "${_gentest_value}")
            else()
                set(GENTEST_SCRIPT "${_gentest_value}")
            endif()
            unset(_gentest_value)
            set(_gentest_current_multi "")
        elseif(_gentest_arg STREQUAL "ARGS" OR _gentest_arg STREQUAL "ENV_VARS" OR _gentest_arg STREQUAL "DEFINES")
            set(_gentest_current_multi "${_gentest_arg}")
        else()
            if(_gentest_current_multi STREQUAL "")
                message(FATAL_ERROR "gentest_add_cmake_script_test: unexpected argument '${_gentest_arg}'")
            endif()

            # Preserve literal semicolons inside list-valued arguments. Fresh
            # CI configures surfaced this through EXPECT_SUBSTRING values that
            # matched the summary line ("passed 1/1; failed 0; ..."), which a
            # regular CMake list would flatten into separate items before we
            # ever escape them for `cmake -D...`.
            string(REPLACE ";" "\\;" _gentest_escaped_arg "${_gentest_arg}")
            if(_gentest_current_multi STREQUAL "ARGS")
                list(APPEND GENTEST_ARGS "${_gentest_escaped_arg}")
            elseif(_gentest_current_multi STREQUAL "ENV_VARS")
                list(APPEND GENTEST_ENV_VARS "${_gentest_escaped_arg}")
            else()
                list(APPEND GENTEST_DEFINES "${_gentest_escaped_arg}")
            endif()
            unset(_gentest_escaped_arg)
        endif()

        math(EXPR _gentest_index "${_gentest_index} + 1")
    endwhile()
    unset(_gentest_arg)
    unset(_gentest_current_multi)
    unset(_gentest_index)

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

    set(_emu_value "")
    if(NOT GENTEST_NO_EMULATOR AND CMAKE_CROSSCOMPILING AND DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND NOT CMAKE_CROSSCOMPILING_EMULATOR STREQUAL "")
        set(_gentest_emu_items ${CMAKE_CROSSCOMPILING_EMULATOR})
        list(LENGTH _gentest_emu_items _gentest_emu_count)
        string(JOIN ";" _gentest_emu_joined ${_gentest_emu_items})
        if(_gentest_emu_count EQUAL 1)
            # Keep the encoded value list-shaped so helper scripts never fall back
            # to shell-style splitting for single paths containing spaces.
            string(APPEND _gentest_emu_joined ";")
        endif()
        unset(_gentest_emu_count)
        unset(_gentest_emu_items)
        set(_emu_value "${_gentest_emu_joined}")
        unset(_gentest_emu_joined)
    endif()

    set(_args_value "")
    if(GENTEST_ARGS)
        string(JOIN ";" _args_joined ${GENTEST_ARGS})
        string(REPLACE "\\;" ";" _args_value "${_args_joined}")
        unset(_args_joined)
    endif()

    set(_env_value "")
    if(GENTEST_ENV_VARS)
        string(JOIN ";" _env_joined ${GENTEST_ENV_VARS})
        string(REPLACE "\\;" ";" _env_value "${_env_joined}")
        unset(_env_joined)
    endif()

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake_script_tests")
    string(MAKE_C_IDENTIFIER "${GENTEST_NAME}" _gentest_wrapper_stem)
    string(SHA256 _gentest_wrapper_hash "${GENTEST_NAME}")
    string(SUBSTRING "${_gentest_wrapper_hash}" 0 12 _gentest_wrapper_hash_short)
    set(_gentest_wrapper_script
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_script_tests/${_gentest_wrapper_stem}_${_gentest_wrapper_hash_short}-$<CONFIG>.cmake")

    set(_gentest_wrapper_content "set(PROG [==[${_prog}]==])\n")
    if(NOT _args_value STREQUAL "")
        string(APPEND _gentest_wrapper_content "set(ARGS [==[${_args_value}]==])\n")
    endif()
    if(NOT _env_value STREQUAL "")
        string(APPEND _gentest_wrapper_content "set(ENV_VARS [==[${_env_value}]==])\n")
    endif()
    if(NOT _emu_value STREQUAL "")
        string(APPEND _gentest_wrapper_content "set(EMU [==[${_emu_value}]==])\n")
    endif()
    foreach(def IN LISTS GENTEST_DEFINES)
        string(REPLACE "\\;" ";" _gentest_define_raw "${def}")
        string(FIND "${_gentest_define_raw}" "=" _gentest_define_eq_pos)
        if(_gentest_define_eq_pos EQUAL -1)
            message(FATAL_ERROR "gentest_add_cmake_script_test: DEFINES entry '${_gentest_define_raw}' must use KEY=VALUE")
        endif()
        string(SUBSTRING "${_gentest_define_raw}" 0 ${_gentest_define_eq_pos} _gentest_define_name)
        if(NOT _gentest_define_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR
                "gentest_add_cmake_script_test: invalid define name '${_gentest_define_name}' in '${_gentest_define_raw}'")
        endif()
        string(LENGTH "${_gentest_define_raw}" _gentest_define_len)
        math(EXPR _gentest_define_value_pos "${_gentest_define_eq_pos} + 1")
        if(_gentest_define_value_pos LESS _gentest_define_len)
            string(SUBSTRING "${_gentest_define_raw}" ${_gentest_define_value_pos} -1 _gentest_define_value)
        else()
            set(_gentest_define_value "")
        endif()
        string(APPEND _gentest_wrapper_content
            "set(${_gentest_define_name} [==[${_gentest_define_value}]==])\n")
        unset(_gentest_define_raw)
        unset(_gentest_define_eq_pos)
        unset(_gentest_define_name)
        unset(_gentest_define_len)
        unset(_gentest_define_value_pos)
        unset(_gentest_define_value)
    endforeach()
    string(APPEND _gentest_wrapper_content "include([==[${GENTEST_SCRIPT}]==])\n")

    file(GENERATE OUTPUT "${_gentest_wrapper_script}" CONTENT "${_gentest_wrapper_content}")

    add_test(NAME ${GENTEST_NAME} COMMAND ${CMAKE_COMMAND} -P "${_gentest_wrapper_script}")
    unset(_gentest_wrapper_content)
    unset(_gentest_wrapper_hash)
    unset(_gentest_wrapper_hash_short)
    unset(_gentest_wrapper_script)
    unset(_gentest_wrapper_stem)
    unset(_args_value)
    unset(_env_value)
    unset(_emu_value)
    unset(_prog)
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
