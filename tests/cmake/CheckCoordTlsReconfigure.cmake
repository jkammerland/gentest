if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja
            -Dgentest_BUILD_TESTING=OFF
            -DGENTEST_BUILD_CODEGEN=OFF
            -DCOORD_ENABLE_TLS=OFF
    RESULT_VARIABLE first_rc
    OUTPUT_VARIABLE first_out
    ERROR_VARIABLE first_err)

if(NOT first_rc EQUAL 0)
    message(FATAL_ERROR
        "initial TLS-off configure failed unexpectedly\n"
        "stdout:\n${first_out}\n"
        "stderr:\n${first_err}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja
            -Dgentest_BUILD_TESTING=OFF
            -DGENTEST_BUILD_CODEGEN=OFF
            -DCOORD_ENABLE_TLS=ON
    RESULT_VARIABLE second_rc
    OUTPUT_VARIABLE second_out
    ERROR_VARIABLE second_err)

if(NOT second_rc EQUAL 0)
    message(FATAL_ERROR
        "TLS re-enable configure failed in the same build dir\n"
        "stdout:\n${second_out}\n"
        "stderr:\n${second_err}")
endif()
