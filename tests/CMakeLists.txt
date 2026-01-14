include(GentestTests)

gentest_resolve_test_std(_gentest_test_std_feature _gentest_codegen_std_flag)
unset(_gentest_test_std_feature)

set(_gentest_test_suites
    unit
    integration
    failing
    skiponly
    fixtures
    templates
    mocking
    ctor
    benchmarks)

# Optionally include libassert integration suite
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_test_suites libassert)
endif()

foreach(suite IN LISTS _gentest_test_suites)
    gentest_add_suite(${suite})
endforeach()

unset(_gentest_test_suites)

# Concurrency target (no default add_test to avoid suite-wide failure)
gentest_add_suite(concurrency TARGET ${PROJECT_NAME}_concurrency_tests NO_CTEST)

# Outcome semantics target (no default add_test; contains deliberate FAIL/XPASS cases)
gentest_add_suite(outcomes TARGET ${PROJECT_NAME}_outcomes_tests NO_CTEST)

# Focused checks: ensure pass-only selection passes, and failing selection returns non-zero
gentest_add_check_counts(
    NAME concurrency_pass_single
    PROG $<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run-test=concurrency/child_expect_pass)

gentest_add_check_death(
    NAME concurrency_fail_single_death
    PROG ${PROJECT_NAME}_concurrency_tests
    EXPECT_SUBSTRING "gentest: fatal: assertion/expectation recorded without an active test context."
    ARGS --run-test=concurrency/child_expect_fail)

gentest_add_check_counts(
    NAME concurrency_multi_pass_single
    PROG $<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run-test=concurrency/multi_adopt_expect_pass)

gentest_add_check_counts(
    NAME concurrency_multi_fail_single
    PROG $<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
    PASS 0
    FAIL 1
    SKIP 0
    ARGS --run-test=concurrency/multi_adopt_expect_fail)

gentest_add_check_death(
    NAME concurrency_multi_noadopt_death
    PROG ${PROJECT_NAME}_concurrency_tests
    EXPECT_SUBSTRING "gentest: fatal: assertion/expectation recorded without an active test context."
    ARGS --run-test=concurrency/no_adopt_expect_death_multi)

option(GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS "Skip Windows Debug death tests that hang on CI" OFF)
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
    set_tests_properties(concurrency_fail_single_death concurrency_multi_noadopt_death PROPERTIES DISABLED TRUE)
endif()

set(_gentest_codegen_prog "")
if(GENTEST_CODEGEN_EXECUTABLE)
    set(_gentest_codegen_prog "${GENTEST_CODEGEN_EXECUTABLE}")
elseif(GENTEST_CODEGEN_TARGET)
    if(NOT TARGET ${GENTEST_CODEGEN_TARGET})
        message(FATAL_ERROR "tests: GENTEST_CODEGEN_TARGET='${GENTEST_CODEGEN_TARGET}' does not exist")
    endif()
    set(_gentest_codegen_prog "$<TARGET_FILE:${GENTEST_CODEGEN_TARGET}>")
elseif(TARGET gentest_codegen)
    set(_gentest_codegen_prog "$<TARGET_FILE:gentest_codegen>")
else()
    message(FATAL_ERROR "tests: no gentest code generator available (gentest_codegen). Configure with -DGENTEST_BUILD_CODEGEN=ON or -DGENTEST_CODEGEN_EXECUTABLE=<path>.")
endif()

# Helper target used to populate compile_commands.json for generator regression tests.
add_library(gentest_codegen_pp_flags OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke/pp_flags_a.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke/pp_flags_b.cpp)
target_compile_features(gentest_codegen_pp_flags PRIVATE cxx_std_20)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/smoke/pp_flags_a.cpp PROPERTIES
    COMPILE_DEFINITIONS "GENTEST_PPFLAGS_DRIFT=1")

# Lint-only checks for generator
add_test(NAME gentest_codegen_check_valid
    COMMAND ${_gentest_codegen_prog}
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
        -- ${_gentest_codegen_std_flag}
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME gentest_codegen_check_invalid
    COMMAND ${_gentest_codegen_prog}
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_std_flag}
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

set_tests_properties(gentest_codegen_check_invalid PROPERTIES WILL_FAIL TRUE)

gentest_add_check_death(
    NAME gentest_codegen_baseline_requires_bench
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "'baseline' requires 'bench' or 'jitter'"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/baseline_requires_bench.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_pp_flags_drift
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "per-source preprocessor flags differ across scanned sources"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/pp_flags_a.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/pp_flags_b.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

set(_gentest_iquote_out "${CMAKE_CURRENT_BINARY_DIR}/smoke/iquote/test_impl.cpp")
set(_gentest_iquote_decls "${CMAKE_CURRENT_BINARY_DIR}/smoke/iquote/test_decls.hpp")
gentest_add_run_and_check_file(
    NAME gentest_codegen_iquote_rewrite
    PROG ${_gentest_codegen_prog}
    FILE ${_gentest_iquote_decls}
    EXPECT_SUBSTRING "#include \"iq.hpp\""
    ARGS
        --output
        ${_gentest_iquote_out}
        --test-decls
        ${_gentest_iquote_decls}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/iquote_include.cpp
        --
        ${_gentest_codegen_std_flag}
        -Wno-unknown-attributes
        -Wno-attributes
        -iquote../../tests/smoke/iquote_root)
set_tests_properties(gentest_codegen_iquote_rewrite PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
unset(_gentest_iquote_out)
unset(_gentest_iquote_decls)

gentest_add_check_death(
    NAME gentest_codegen_internal_linkage_test
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "tests with internal linkage are not supported"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/internal_linkage.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_signature_type_defined_in_cpp
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "type 'LocalSignatureType' used by test signature must be defined in a header"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/signature_type_defined_in_cpp.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

# Generator negative tests for mocks: robust diagnostics
gentest_add_check_death(
    NAME gentest_codegen_mock_incomplete_virtual_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "gentest::mock<T>: target type is incomplete"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/incomplete_virtual_ref.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_virtual_defined_in_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "polymorphic target appears defined in a source file"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/virtual_defined_in_cpp.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_anonymous_namespace_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "cannot mock a type in an anonymous namespace"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/anonymous_namespace_type.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_local_class_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "cannot mock a local class"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/local_class.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

# Generator negative tests for fixtures: disallow member tests on suite/global under strict mode
gentest_add_check_death(
    NAME gentest_codegen_fixture_suite_member_illegal
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "suite fixtures cannot declare member tests"
    ENV_VARS GENTEST_STRICT_FIXTURE=1
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/suite_member_test.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_fixture_global_member_illegal
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "global fixtures cannot declare member tests"
    ENV_VARS GENTEST_STRICT_FIXTURE=1
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/global_member_test.cpp
        --
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

# End-to-end behavioral checks with robust counting
gentest_add_check_counts(NAME unit_counts PROG $<TARGET_FILE:gentest_unit_tests> PASS 8 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME unit_list_counts PROG $<TARGET_FILE:gentest_unit_tests> LIST CASES 8)

gentest_add_check_counts(NAME integration_counts PROG $<TARGET_FILE:gentest_integration_tests> PASS 3 FAIL 0 SKIP 1)
gentest_add_check_counts(NAME integration_list_counts PROG $<TARGET_FILE:gentest_integration_tests> LIST CASES 4)

gentest_add_check_counts(NAME failing_counts PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 3 SKIP 0)

# The raw failing suite run is expected to fail
set_tests_properties(failing PROPERTIES WILL_FAIL TRUE)

# Verify argument-mismatch message content for mock expectations


gentest_add_check_counts(NAME skiponly_counts PROG $<TARGET_FILE:gentest_skiponly_tests> PASS 0 FAIL 0 SKIP 2)
gentest_add_check_counts(NAME skiponly_list_counts PROG $<TARGET_FILE:gentest_skiponly_tests> LIST CASES 2)

gentest_add_check_counts(NAME fixtures_counts PROG $<TARGET_FILE:gentest_fixtures_tests> PASS 7 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME fixtures_list_counts PROG $<TARGET_FILE:gentest_fixtures_tests> LIST CASES 7)

gentest_add_check_counts(NAME templates_counts PROG $<TARGET_FILE:gentest_templates_tests> PASS 93 FAIL 0 SKIP 0)
gentest_add_check_lines(NAME templates_list_tests_lines PROG $<TARGET_FILE:gentest_templates_tests> LINES 93 ARGS --list-tests)

gentest_add_check_counts(NAME mocking_counts PROG $<TARGET_FILE:gentest_mocking_tests> PASS 24 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME mocking_list_counts PROG $<TARGET_FILE:gentest_mocking_tests> LIST CASES 24)

# Runtime skip/xfail/XPASS outcome checks
gentest_add_check_counts(
    NAME outcomes_runtime_skip_simple
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 1
    XFAIL 0
    XPASS 0
    ARGS --run-test=outcomes/runtime_skip_simple)

gentest_add_check_counts(
    NAME outcomes_runtime_skip_if
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 1
    XFAIL 0
    XPASS 0
    ARGS --run-test=outcomes/runtime_skip_if)

gentest_add_check_counts(
    NAME outcomes_xfail_expect_fail
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 0
    XFAIL 1
    XPASS 0
    ARGS --run-test=outcomes/xfail_expect_fail)

gentest_add_check_counts(
    NAME outcomes_xfail_throw
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 0
    XFAIL 1
    XPASS 0
    ARGS --run-test=outcomes/xfail_throw)

gentest_add_check_counts(
    NAME outcomes_xfail_xpass
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 0
    XFAIL 0
    XPASS 1
    ARGS --run-test=outcomes/xfail_xpass)

gentest_add_check_counts(
    NAME outcomes_skip_overrides_xfail
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 0
    SKIP 1
    XFAIL 0
    XPASS 0
    ARGS --run-test=outcomes/skip_overrides_xfail)

gentest_add_check_counts(
    NAME outcomes_skip_after_failure_is_fail
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    PASS 0
    FAIL 1
    SKIP 0
    XFAIL 0
    XPASS 0
    ARGS --run-test=outcomes/skip_after_failure_is_fail)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xfail_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml
    EXPECT_SUBSTRING "message=\"xfail: expected failure\""
    ARGS --run-test=outcomes/xfail_expect_fail --junit=${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xpass_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xpass.xml
    EXPECT_SUBSTRING "xpass: unexpected pass"
    ARGS --run-test=outcomes/xfail_xpass --junit=${CMAKE_BINARY_DIR}/outcomes_junit_xpass.xml)

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

    gentest_add_check_counts(
        NAME libassert_counts
        PROG $<TARGET_FILE:${PROJECT_NAME}_libassert_tests>
        PASS 5
        FAIL 6
        SKIP 0)

    # The raw suite run is expected to fail due to failing tests
    set_tests_properties(libassert PROPERTIES WILL_FAIL TRUE)
endif()

# Optional: no-exceptions death test for libassert integration
if(GENTEST_WITH_LIBASSERT AND GENTEST_WITH_LIBASSERT_NOEXCEPT_TESTS)
    set(target_name_noexc "${PROJECT_NAME}_libassert_noexc_tests")
    gentest_add_suite(libassert TARGET ${target_name_noexc} OUTPUT_DIR libassert_noexc NO_CTEST)

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

    # Link libassert into this target
    if(TARGET libassert-lib)
        target_link_libraries(${target_name_noexc} PRIVATE libassert-lib)
    elseif(TARGET libassert::assert)
        target_link_libraries(${target_name_noexc} PRIVATE libassert::assert)
    else()
        message(FATAL_ERROR "libassert not found for no-exceptions test")
    endif()

    # Death test: fatal assertion must terminate and print the adapter's message
    gentest_add_check_death(
        NAME libassert_noexc_assert_fail_death
        PROG ${target_name_noexc}
        EXPECT_SUBSTRING "gentest: exceptions are disabled; terminating after fatal assertion"
        ARGS --run-test=libassert/assert_fail_simple)
endif()

# Verify descriptive predicate mismatch content
gentest_add_check_death(
    NAME mocking_predicate_mismatch_message
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "expected == 3"
    ARGS --run-test=failing/mocking/predicate_mismatch)

# GitHub annotations smoke: ensure ::error lines present when enabled
gentest_add_check_death(
    NAME github_annotations_smoke
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "::error file="
    ARGS
        --run-test=failing/mocking/predicate_mismatch
        --github-annotations)

# CLI checks for VSCode integration
gentest_add_check_contains(NAME unit_help PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING gentest ARGS --help)
gentest_add_check_lines(NAME unit_list_tests_lines PROG $<TARGET_FILE:gentest_unit_tests> LINES 8 ARGS --list-tests)
gentest_add_check_lines(NAME integration_list_tests_lines PROG $<TARGET_FILE:gentest_integration_tests> LINES 4 ARGS --list-tests)

gentest_add_check_counts(
    NAME run_test_exact
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run-test=unit/arithmetic/sum)

gentest_add_check_counts(
    NAME run_test_default_name_free
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run-test=unit/default_name_free)

gentest_add_check_counts(
    NAME run_test_default_name_member
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run-test=unit/DefaultNameFixture/default_name_member)

gentest_add_check_exit_code(
    NAME run_test_not_found
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_RC 1
    ARGS --run-test=does/not/exist)

gentest_add_check_counts(
    NAME filter_single
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --filter=*strings*)

gentest_add_check_lines(NAME fixtures_list_tests_lines PROG $<TARGET_FILE:gentest_fixtures_tests> LINES 7 ARGS --list-tests)

# JUnit XML smoke test: generate and validate basic content
gentest_add_check_file_contains(
    NAME junit_smoke_unit
    PROG $<TARGET_FILE:gentest_unit_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml
    EXPECT_SUBSTRING <testsuite
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml)

gentest_add_check_file_contains(
    NAME junit_properties_unit
    PROG $<TARGET_FILE:gentest_unit_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml
    EXPECT_SUBSTRING "<property name=\"requirement\" value=\"#42\""
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml)

gentest_add_check_counts(NAME repeat_unit_twice PROG $<TARGET_FILE:gentest_unit_tests> PASS 16 FAIL 0 SKIP 0 ARGS --repeat=2)

gentest_add_check_counts(NAME failing_fail_fast PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 1 SKIP 0 ARGS --fail-fast)

gentest_add_check_contains(
    NAME shuffle_seed_print
    PROG $<TARGET_FILE:gentest_fixtures_tests>
    EXPECT_SUBSTRING "Shuffle seed: 42"
    ARGS
        --run-test=fixtures/free/basic
        --shuffle
        --seed
        42)

option(GENTEST_ENABLE_ALLURE_TESTS "Enable Allure writer tests (off by default)" OFF)
if(GENTEST_ENABLE_ALLURE_TESTS)
    # Allure results smoke: single test writes a result file with passed status
    gentest_add_check_file_contains(
        NAME allure_smoke_single
        PROG $<TARGET_FILE:gentest_unit_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single/result-0-result.json
        EXPECT_SUBSTRING "status\": \"passed"
        ARGS
            --run-test=unit/arithmetic/sum
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single)
    set_tests_properties(allure_smoke_single PROPERTIES LABELS "allure")

    # Allure logs attachment: failing test with log_on_fail(true) should create a logs attachment
    gentest_add_run_and_check_file(
        NAME allure_logs_attachment
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-result.json
        EXPECT_SUBSTRING "\"attachments\": [ { \"name\": \"logs\", \"source\": \"result-0-attachment.txt\""
        ARGS
            --run-test=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs)
    set_tests_properties(allure_logs_attachment PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_file
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-attachment.txt
        EXPECT_SUBSTRING "hello from log"
        ARGS
            --run-test=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs)
    set_tests_properties(allure_logs_attachment_file PROPERTIES LABELS "allure")
endif()

# Capture JUnit artifact example: unit suite emits JUnit; this smoke ensures the file is generated in build tree
gentest_add_check_file_contains(
    NAME junit_artifact_example
    PROG $<TARGET_FILE:gentest_integration_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml
    EXPECT_SUBSTRING <testsuite
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml)

# Constructor exception handling across fixture lifetimes
gentest_add_check_death(
    NAME fixture_ctor_ephemeral_member
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "unexpected std::exception: ephemeral-ctor"
    ARGS --run-test=ctor/ephemeral_member)

gentest_add_check_death(
    NAME fixture_ctor_free_fixtures
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "unexpected std::exception: free-fx-ctor"
    ARGS --run-test=ctor/free_fixtures)

gentest_add_check_death(
    NAME fixture_ctor_suite_shared
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "fixture construction threw std::exception: suite-ctor"
    ARGS --run-test=ctor/suite)

gentest_add_check_death(
    NAME fixture_ctor_global_shared
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "fixture construction threw std::exception: global-ctor"
    ARGS --run-test=ctor/global)
# The 'ctor' suite intentionally contains failing tests; mark its raw run as WILL_FAIL
set_tests_properties(ctor PROPERTIES WILL_FAIL TRUE)
# Benchmarks: smoke checks for listing and suite table
gentest_add_check_contains(
    NAME benches_list_benches
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING benchmarks/string/concat_small
    ARGS --list-benches)

gentest_add_check_contains(
    NAME benches_table_smoke
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING benchmarks/string/concat_small
    ARGS --list)

# Jitter: histogram header and suite table header
gentest_add_check_contains(
    NAME jitter_hist_header
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING benchmarks/math/sin_jitter
    ARGS --list)

gentest_add_check_contains(
    NAME jitter_suite_table_smoke
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING benchmarks/math/sin_jitter
    ARGS --list)

add_test(NAME gentest_codegen_output_collision
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_collision
        -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
        -DGENERATOR=${CMAKE_GENERATOR}
        -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
        -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenOutputCollision.cmake)

set_tests_properties(gentest_codegen_output_collision PROPERTIES LABELS "cmake")

set(_gentest_discover_tests_config "")
if(CMAKE_CONFIGURATION_TYPES)
    set(_gentest_discover_tests_config "Debug")
endif()

add_test(NAME gentest_discover_tests_smoke
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/cmake/discover_tests
        -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
        -DGENERATOR=${CMAKE_GENERATOR}
        -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
        -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_CONFIG=${_gentest_discover_tests_config}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckDiscoverTests.cmake)

set_tests_properties(gentest_discover_tests_smoke PROPERTIES LABELS "cmake")

unset(_gentest_discover_tests_config)

option(GENTEST_ENABLE_PACKAGE_TESTS "Enable install + find_package consumer smoke test (FetchContent)" OFF)
if(GENTEST_ENABLE_PACKAGE_TESTS)
    set(_gentest_package_test_config "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_gentest_package_test_config "Debug")
    endif()

    add_test(NAME gentest_package_consumer
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DPACKAGE_NAME=${PROJECT_NAME}
            -DGENERATOR=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DBUILD_CONFIG=${_gentest_package_test_config}
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake)

    set_tests_properties(gentest_package_consumer PROPERTIES LABELS "package")
endif()
