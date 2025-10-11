set(_gentest_test_suites
    unit
    integration
    failing
    skiponly
    fixtures
    templates
    mocking
    benchmarks)

# Optionally include libassert integration suite
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_test_suites libassert)
endif()

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
        -DPASS=5 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME unit_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DLIST=ON -DCASES=5
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
        -DPASS=0 -DFAIL=3 -DSKIP=0
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
        -DPASS=93 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME templates_list_tests_lines
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_templates_tests>
        -DLINES=93
        -DARGS=--list-tests
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckLines.cmake)

add_test(NAME mocking_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_mocking_tests>
        -DPASS=19 -DFAIL=0 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME mocking_list_counts
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_mocking_tests>
        -DLIST=ON -DCASES=19
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

# libassert integration counts (optional)
if(GENTEST_WITH_LIBASSERT)
    # Link libassert into the generated test target (support either upstream target name)
    if(TARGET libassert-lib)
        target_link_libraries(${PROJECT_NAME}_libassert_tests PRIVATE libassert-lib)
    elseif(TARGET libassert::assert)
        target_link_libraries(${PROJECT_NAME}_libassert_tests PRIVATE libassert::assert)
    else()
        message(FATAL_ERROR "libassert not found: enable FetchContent or provide a libassert package")
    endif()

    add_test(NAME libassert_counts
        COMMAND ${CMAKE_COMMAND}
            -DPROG=$<TARGET_FILE:${PROJECT_NAME}_libassert_tests>
            -DPASS=5 -DFAIL=6 -DSKIP=0
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

    # The raw suite run is expected to fail due to failing tests
    set_tests_properties(libassert PROPERTIES WILL_FAIL TRUE)
endif()

# Optional: no-exceptions death test for libassert integration
if(GENTEST_WITH_LIBASSERT AND GENTEST_WITH_LIBASSERT_NOEXCEPT_TESTS)
    set(target_name_noexc "${PROJECT_NAME}_libassert_noexc_tests")

    add_executable(${target_name_noexc}
        support/test_entry.cpp)

    target_link_libraries(${target_name_noexc}
        PRIVATE
            ${PROJECT_NAME}
            fmt::fmt)

    target_compile_features(${target_name_noexc} PRIVATE cxx_std_23)
    target_include_directories(${target_name_noexc}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR})

    # Disable exceptions for this target (Clang/GNU/AppleClang); MSVC uses different flags
    target_compile_options(${target_name_noexc}
        PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-fno-exceptions>
            $<$<CXX_COMPILER_ID:MSVC>:/EHs-c->)

    # Disable library throws where supported (fmt/MSVC STL); clang-cl uses Clang ID but MSVC STL
    target_compile_definitions(${target_name_noexc}
        PRIVATE
            FMT_EXCEPTIONS=0
            _HAS_EXCEPTIONS=0)

    gentest_attach_codegen(${target_name_noexc}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libassert_noexc/test_impl.cpp
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/libassert/cases.cpp
        CLANG_ARGS
            -std=c++23
            -Wno-unknown-attributes
            -Wno-attributes
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Link libassert into this target
    if(TARGET libassert-lib)
        target_link_libraries(${target_name_noexc} PRIVATE libassert-lib)
    elseif(TARGET libassert::assert)
        target_link_libraries(${target_name_noexc} PRIVATE libassert::assert)
    else()
        message(FATAL_ERROR "libassert not found for no-exceptions test")
    endif()

    # Death test: fatal assertion must terminate and print the adapter's message
    add_test(NAME libassert_noexc_assert_fail_death
        COMMAND ${CMAKE_COMMAND}
            -DPROG=$<TARGET_FILE:${target_name_noexc}>
            -DARGS=--run-test=libassert/assert_fail_simple
            -DEXPECT_SUBSTRING=gentest: exceptions are disabled; terminating after fatal assertion
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake)
endif()

# Verify descriptive predicate mismatch content
add_test(NAME mocking_predicate_mismatch_message
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DARGS=--run-test=failing/mocking/predicate_mismatch
        -DEXPECT_SUBSTRING=expected == 3
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckDeath.cmake)

# GitHub annotations smoke: ensure ::error lines present when enabled
add_test(NAME github_annotations_smoke
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DARGS=--run-test=failing/mocking/predicate_mismatch\ --github-annotations
        -DEXPECT_SUBSTRING=::error\ file=
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
        -DLINES=5
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

# JUnit XML smoke test: generate and validate basic content
add_test(NAME junit_smoke_unit
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml
        -DEXPECT_SUBSTRING=<testsuite name=\"unit\"
        -DARGS=--junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake)

add_test(NAME junit_properties_unit
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml
        -DEXPECT_SUBSTRING=<property name=\"requirement\" value=\"#42\"
        -DARGS=--junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake)

add_test(NAME repeat_unit_twice
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DPASS=10 -DFAIL=0 -DSKIP=0
        -DARGS=--repeat=2
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME failing_fail_fast
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DPASS=0 -DFAIL=1 -DSKIP=0
        -DARGS=--fail-fast
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

add_test(NAME shuffle_seed_print
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_fixtures_tests>
        -DEXPECT_SUBSTRING=Shuffle seed: 42
        -DARGS=--run-test=fixtures/free/basic\ --shuffle-fixtures\ --seed\ 42
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)

# Allure results smoke: single test writes a result file with passed status
add_test(NAME allure_smoke_single
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_unit_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single/result-0-result.json
        -DEXPECT_SUBSTRING=status\":\ \"passed
        -DARGS=--run-test=unit/arithmetic/sum\ --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake)

# Allure logs attachment: failing test with log_on_fail(true) should create a logs attachment
add_test(NAME allure_logs_attachment
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-result.json
        -DEXPECT_SUBSTRING=\"attachments\":\ \[\ {\ \"name\"\:\ \"logs\"\,\ \"source\"\:\ \"result-0-attachment.txt\"
        -DARGS=--run-test=failing/logging/attachment\ --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs
        -P ${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake)

add_test(NAME allure_logs_attachment_file
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_failing_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-attachment.txt
        -DEXPECT_SUBSTRING=hello from log
        -DARGS=--run-test=failing/logging/attachment\ --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs
        -P ${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake)

# Capture JUnit artifact example: unit suite emits JUnit; this smoke ensures the file is generated in build tree
add_test(NAME junit_artifact_example
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_integration_tests>
        -DFILE=${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml
        -DEXPECT_SUBSTRING=<testsuite
        -DARGS=--junit=${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake)

# Benchmarks: smoke checks for listing and suite table
add_test(NAME benches_list_benches
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_benchmarks_tests>
        -DARGS=--list-benches
        -DEXPECT_SUBSTRING=benchmarks/string/concat_small
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)

add_test(NAME benches_table_smoke
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_benchmarks_tests>
        -DARGS=--bench-filter=benchmarks/*\ --bench-table
        -DEXPECT_SUBSTRING=Summary\ \(benchmarks\)
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)

# Jitter: histogram header and suite table header
add_test(NAME jitter_hist_header
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_benchmarks_tests>
        -DARGS=--run-jitter=benchmarks/math/sin_jitter\ --jitter-bins=5
        -DEXPECT_SUBSTRING=histogram\ \(
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)

add_test(NAME jitter_suite_table_smoke
    COMMAND ${CMAKE_COMMAND}
        -DPROG=$<TARGET_FILE:gentest_benchmarks_tests>
        -DARGS=--jitter-filter=benchmarks/*
        -DEXPECT_SUBSTRING=Jitter\ \(benchmarks\)
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckContains.cmake)
