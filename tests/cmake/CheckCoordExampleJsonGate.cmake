if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()
if(NOT DEFINED CODEGEN)
    message(FATAL_ERROR "CODEGEN is required")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja
            -Dgentest_BUILD_TESTING=ON
            -DGENTEST_BUILD_CODEGEN=OFF
            -DGENTEST_CODEGEN_EXECUTABLE=${CODEGEN}
            -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
            -DCOORD_BUILD=ON
            -DCOORD_ENABLE_JSON=OFF
    RESULT_VARIABLE configure_rc
    OUTPUT_VARIABLE configure_out
    ERROR_VARIABLE configure_err)

if(NOT configure_rc EQUAL 0)
    message(FATAL_ERROR
        "coord JSON-off scratch configure failed unexpectedly\n"
        "stdout:\n${configure_out}\n"
        "stderr:\n${configure_err}")
endif()

execute_process(
    COMMAND ctest --test-dir "${BUILD_DIR}" -N
    RESULT_VARIABLE list_rc
    OUTPUT_VARIABLE list_out
    ERROR_VARIABLE list_err)

if(NOT list_rc EQUAL 0)
    message(FATAL_ERROR
        "ctest -N failed unexpectedly\n"
        "stdout:\n${list_out}\n"
        "stderr:\n${list_err}")
endif()

foreach(name IN ITEMS coordd_udp_fixture udp_multi_node_mode_a coordd_udp_cleanup)
    if(list_out MATCHES "(^|[ \t\n])${name}([ \t\n]|$)")
        message(FATAL_ERROR
            "coord example tests must not be registered when JSON support is disabled\n"
            "offending test: ${name}\n"
            "ctest -N output:\n${list_out}")
    endif()
endforeach()
