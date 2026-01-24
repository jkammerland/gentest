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
        support/test_entry.cpp
        ${GENTEST_CASES})

    target_link_libraries(${GENTEST_TARGET}
        PRIVATE
            ${PROJECT_NAME}
            fmt::fmt)

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
            -Wno-unknown-attributes
            -Wno-attributes
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR})
    unset(_gentest_codegen_std_arg)
    unset(_gentest_target_std_feature)

    if(NOT GENTEST_NO_CTEST)
        add_test(NAME ${suite} COMMAND ${GENTEST_TARGET})
    endif()
endfunction()

function(gentest_add_cmake_script_test)
    set(options NO_EMULATOR)
    set(one_value_args NAME PROG SCRIPT)
    set(multi_value_args ARGS ENV_VARS DEFINES)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

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
    set(_prog_def "-DPROG=${_prog}")
    unset(_prog)

    set(_emu_def "")
    if(NOT GENTEST_NO_EMULATOR AND CMAKE_CROSSCOMPILING AND DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND NOT CMAKE_CROSSCOMPILING_EMULATOR STREQUAL "")
        string(JOIN " " _gentest_emu_joined ${CMAKE_CROSSCOMPILING_EMULATOR})
        set(_emu_def "-DEMU=${_gentest_emu_joined}")
        unset(_gentest_emu_joined)
    endif()

    set(_args_def "")
    if(GENTEST_ARGS)
        string(JOIN ";" _args_joined ${GENTEST_ARGS})
        string(REPLACE ";" "\\;" _args_escaped "${_args_joined}")
        set(_args_def "-DARGS=${_args_escaped}")
        unset(_args_escaped)
    endif()

    set(_env_def "")
    if(GENTEST_ENV_VARS)
        string(JOIN ";" _env_joined ${GENTEST_ENV_VARS})
        string(REPLACE ";" "\\;" _env_escaped "${_env_joined}")
        set(_env_def "-DENV_VARS=${_env_escaped}")
        unset(_env_escaped)
    endif()

    set(_defines)
    foreach(def IN LISTS GENTEST_DEFINES)
        list(APPEND _defines "-D${def}")
    endforeach()

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
    list(APPEND _cmd_args ${_defines} -P "${GENTEST_SCRIPT}")

    add_test(NAME ${GENTEST_NAME} COMMAND ${CMAKE_COMMAND} ${_cmd_args})
    set_property(TEST ${GENTEST_NAME} APPEND PROPERTY LABELS "cmake")
endfunction()

function(gentest_add_check_counts)
    set(options LIST)
    set(one_value_args NAME PROG PASS FAIL SKIP XFAIL XPASS CASES)
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
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES ${_defines})
endfunction()

function(gentest_add_check_exit_code)
    set(one_value_args NAME PROG EXPECT_RC)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR GENTEST_EXPECT_RC STREQUAL "")
        message(FATAL_ERROR "gentest_add_check_exit_code: NAME, PROG, and EXPECT_RC are required")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckExitCode.cmake"
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
    set(one_value_args NAME PROG FILE EXPECT_SUBSTRING)
    set(multi_value_args ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_NAME OR NOT GENTEST_PROG OR NOT GENTEST_FILE OR GENTEST_EXPECT_SUBSTRING STREQUAL "")
        message(FATAL_ERROR "gentest_add_run_and_check_file: NAME, PROG, FILE, and EXPECT_SUBSTRING are required")
    endif()

    gentest_add_cmake_script_test(
        NAME ${GENTEST_NAME}
        PROG ${GENTEST_PROG}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
        ARGS ${GENTEST_ARGS}
        DEFINES
            "FILE=${GENTEST_FILE}"
            "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}")
endfunction()
