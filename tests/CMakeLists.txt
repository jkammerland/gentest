set(_gentest_test_suites
    unit
    integration
    failing
    skiponly
    fixtures
    templates
    mocking)

foreach(suite IN LISTS _gentest_test_suites)
    set(target_name "${PROJECT_NAME}_${suite}_tests")

    add_executable(${target_name}
        support/test_entry.cpp)

    target_link_libraries(${target_name}
        PRIVATE
            ${PROJECT_NAME}
            fmt::fmt)

    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_include_directories(${target_name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR})

    gentest_attach_codegen(${target_name}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${suite}/test_impl.cpp
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${suite}/cases.cpp
        CLANG_ARGS
            -std=c++23
            -Wno-unknown-attributes
            -Wno-attributes
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME ${suite}
        COMMAND ${target_name})
endforeach()

unset(_gentest_test_suites)

# Concurrency target (no default add_test to avoid suite-wide failure)
add_executable(${PROJECT_NAME}_concurrency_tests
    support/test_entry.cpp)
target_link_libraries(${PROJECT_NAME}_concurrency_tests PRIVATE ${PROJECT_NAME} fmt::fmt)
target_compile_features(${PROJECT_NAME}_concurrency_tests PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME}_concurrency_tests PRIVATE ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
gentest_attach_codegen(${PROJECT_NAME}_concurrency_tests
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/concurrency/test_impl.cpp
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/concurrency/cases.cpp
    CLANG_ARGS -std=c++23 -Wno-unknown-attributes -Wno-attributes -I${PROJECT_SOURCE_DIR}/include -I${CMAKE_CURRENT_SOURCE_DIR})

# Focused checks: ensure pass-only selection passes, and failing selection returns non-zero
add_test(NAME concurrency_pass_single
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        -DPASS=1 -DFAIL=0 -DSKIP=0
        -DARGS=--run-test=concurrency/child_expect_pass
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME concurrency_fail_single_death
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        -DARGS=--run-test=concurrency/child_expect_fail
        -DEXPECT_SUBSTRING=gentest: fatal: assertion/expectation recorded without an active test context.
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake)

add_test(NAME concurrency_multi_pass_single
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        -DPASS=1 -DFAIL=0 -DSKIP=0
        -DARGS=--run-test=concurrency/multi_adopt_expect_pass
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME concurrency_multi_fail_single
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        -DPASS=0 -DFAIL=1 -DSKIP=0
        -DARGS=--run-test=concurrency/multi_adopt_expect_fail
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME concurrency_multi_noadopt_death
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        -DARGS=--run-test=concurrency/no_adopt_expect_death_multi
        -DEXPECT_SUBSTRING=gentest: fatal: assertion/expectation recorded without an active test context.
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake)

# Lint-only checks for generator
add_test(NAME gentest_codegen_check_valid
    COMMAND $<TARGET_FILE:gentest_codegen>
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
        -- -std=c++23
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME gentest_codegen_check_invalid
    COMMAND $<TARGET_FILE:gentest_codegen>
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- -std=c++23
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

set_tests_properties(gentest_codegen_check_invalid PROPERTIES WILL_FAIL TRUE)

# End-to-end behavioral checks with robust counting
add_test(NAME unit_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DPASS=3 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME unit_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DLIST=ON -DCASES=3
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME integration_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_integration_tests>
        -DPASS=3 -DFAIL=0 -DSKIP=1
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME integration_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_integration_tests>
        -DLIST=ON -DCASES=4
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME failing_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DPASS=0 -DFAIL=2 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

# The raw failing suite run is expected to fail
set_tests_properties(failing PROPERTIES WILL_FAIL TRUE)

# Verify argument-mismatch message content for mock expectations


add_test(NAME skiponly_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_skiponly_tests>
        -DPASS=0 -DFAIL=0 -DSKIP=2
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME skiponly_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_skiponly_tests>
        -DLIST=ON -DCASES=2
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME fixtures_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_fixtures_tests>
        -DPASS=7 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME fixtures_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_fixtures_tests>
        -DLIST=ON -DCASES=7
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME templates_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_templates_tests>
        -DPASS=71 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME templates_list_tests_lines
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_templates_tests>
        -DLINES=71
        -DARGS=--list-tests
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake)

add_test(NAME mocking_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_mocking_tests>
        -DPASS=18 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME mocking_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_mocking_tests>
        -DLIST=ON -DCASES=18
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

# Verify descriptive predicate mismatch content
add_test(NAME mocking_predicate_mismatch_message
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DARGS=--run-test=failing/mocking/predicate_mismatch
        -DEXPECT_SUBSTRING=expected == 3
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake)

# CLI checks for VSCode integration
add_test(NAME unit_help
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DEXPECT_SUBSTRING=gentest
        -DARGS=--help
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)

add_test(NAME unit_list_tests_lines
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DLINES=3
        -DARGS=--list-tests
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake)

add_test(NAME integration_list_tests_lines
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_integration_tests>
        -DLINES=4
        -DARGS=--list-tests
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake)

add_test(NAME run_test_exact
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DPASS=1 -DFAIL=0 -DSKIP=0
        -DARGS=--run-test=unit/arithmetic/sum
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME run_test_not_found
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DEXPECT_RC=1
        -DARGS=--run-test=does/not/exist
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckExitCode.cmake)

add_test(NAME filter_single
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DPASS=1 -DFAIL=0 -DSKIP=0
        -DARGS=--filter=*strings*
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME fixtures_list_tests_lines
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_fixtures_tests>
        -DLINES=7
        -DARGS=--list-tests
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake)
