include(GentestTests)

if(NOT TARGET gentest_test_suites)
    add_custom_target(gentest_test_suites)
endif()

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

# coord suite depends on coord runtime + coordctl/coordd binaries + JSON support
if(COORD_BUILD AND COORD_ENABLE_JSON AND TARGET coord AND TARGET coordctl AND TARGET coordd)
    list(APPEND _gentest_test_suites coord)
endif()

# Optionally include libassert integration suite
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_test_suites libassert)
endif()

foreach(suite IN LISTS _gentest_test_suites)
    gentest_add_suite(${suite})
endforeach()

if(TARGET gentest_coord_tests)
    find_package(Boost REQUIRED COMPONENTS json)
    target_link_libraries(gentest_coord_tests PRIVATE coord::coord)
    target_link_libraries(gentest_coord_tests PRIVATE Boost::json)
    if(TARGET coordctl AND TARGET coordd)
        add_dependencies(gentest_coord_tests coordctl coordd)
        target_compile_definitions(gentest_coord_tests PRIVATE
            COORDCTL_BIN_PATH="$<TARGET_FILE:coordctl>"
            COORDD_BIN_PATH="$<TARGET_FILE:coordd>")
    endif()
endif()

unset(_gentest_test_suites)

# Concurrency target (no default add_test to avoid suite-wide failure)
gentest_add_suite(concurrency TARGET ${PROJECT_NAME}_concurrency_tests NO_CTEST)

# Outcome semantics target (no default add_test; contains deliberate FAIL/XPASS cases)
gentest_add_suite(outcomes TARGET ${PROJECT_NAME}_outcomes_tests NO_CTEST)

# Benchmark-only target: collect all suite translation units into one target so
# gentest_codegen sees many wrapper sources in a single invocation (useful for
# profiling/benchmarking internal codegen parallelism).
#
# Note: This target is *not* meant to be compiled/linked as a single binary,
# because mock registry generation is per-target and requires all mocked types
# to be visible before including `gentest/mock.h` in each TU. Suites may define
# mocked types in different TUs, which breaks compilation when merged.
#
# Build `gentest_codegen_parallel_bench` (custom target below) to run only the
# codegen step.
add_library(gentest_codegen_parallel_bench_obj OBJECT EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/failing/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/skiponly/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/templates/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocking/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ctor/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/concurrency/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/outcomes/cases.cpp)
if(GENTEST_WITH_LIBASSERT)
    target_sources(gentest_codegen_parallel_bench_obj PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libassert/cases.cpp)
endif()

target_link_libraries(gentest_codegen_parallel_bench_obj PRIVATE gentest_main)

gentest_resolve_test_std(_gentest_parallel_bench_std_feature _gentest_parallel_bench_codegen_std_arg)
target_compile_features(gentest_codegen_parallel_bench_obj PRIVATE ${_gentest_parallel_bench_std_feature})
target_include_directories(gentest_codegen_parallel_bench_obj PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

gentest_attach_codegen(gentest_codegen_parallel_bench_obj
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/codegen_parallel_bench
    CLANG_ARGS
        ${_gentest_parallel_bench_codegen_std_arg}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})
unset(_gentest_parallel_bench_codegen_std_arg)
unset(_gentest_parallel_bench_std_feature)

# User-facing benchmark target: run only the code generation step for the
# multi-TU bench.
add_custom_target(gentest_codegen_parallel_bench
    DEPENDS gentest_codegen_gentest_codegen_parallel_bench_obj)

# Focused checks: ensure pass-only selection passes, and failing selection returns non-zero
set(_gentest_concurrency_count_checks
    "concurrency_pass_single|1|0|0|concurrency/child_expect_pass"
    "concurrency_pass_throw_single|1|0|0|concurrency/child_expect_throw_pass"
    "concurrency_multi_pass_single|1|0|0|concurrency/multi_adopt_expect_pass"
    "concurrency_multi_fail_single|0|1|0|concurrency/multi_adopt_expect_fail")

foreach(_gentest_check IN LISTS _gentest_concurrency_count_checks)
    string(REPLACE "|" ";" _gentest_fields "${_gentest_check}")
    list(GET _gentest_fields 0 _gentest_name)
    list(GET _gentest_fields 1 _gentest_pass)
    list(GET _gentest_fields 2 _gentest_fail)
    list(GET _gentest_fields 3 _gentest_skip)
    list(GET _gentest_fields 4 _gentest_run)

    gentest_add_check_counts(
        NAME ${_gentest_name}
        PROG $<TARGET_FILE:${PROJECT_NAME}_concurrency_tests>
        PASS ${_gentest_pass}
        FAIL ${_gentest_fail}
        SKIP ${_gentest_skip}
        ARGS --run=${_gentest_run})
endforeach()
unset(_gentest_concurrency_count_checks)
unset(_gentest_check)
unset(_gentest_fields)
unset(_gentest_name)
unset(_gentest_pass)
unset(_gentest_fail)
unset(_gentest_skip)
unset(_gentest_run)

set(_gentest_concurrency_death_checks
    "concurrency_fail_single_death|concurrency/child_expect_fail"
    "concurrency_multi_noadopt_death|concurrency/no_adopt_expect_death_multi")

foreach(_gentest_check IN LISTS _gentest_concurrency_death_checks)
    string(REPLACE "|" ";" _gentest_fields "${_gentest_check}")
    list(GET _gentest_fields 0 _gentest_name)
    list(GET _gentest_fields 1 _gentest_run)

    gentest_add_check_death(
        NAME ${_gentest_name}
        PROG ${PROJECT_NAME}_concurrency_tests
        EXPECT_SUBSTRING "gentest: fatal: assertion/expectation recorded without an active test context."
        ARGS --run=${_gentest_run})
endforeach()
unset(_gentest_concurrency_death_checks)
unset(_gentest_check)
unset(_gentest_fields)
unset(_gentest_name)
unset(_gentest_run)

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

set(_gentest_codegen_cross_target_arg "")
if(CMAKE_CROSSCOMPILING)
    if(CMAKE_CXX_COMPILER_TARGET)
        set(_gentest_codegen_cross_target_arg "--target=${CMAKE_CXX_COMPILER_TARGET}")
    elseif(CMAKE_C_COMPILER_TARGET)
        set(_gentest_codegen_cross_target_arg "--target=${CMAKE_C_COMPILER_TARGET}")
    endif()
endif()

# Lint-only checks for generator
add_test(NAME gentest_codegen_check_valid
    COMMAND ${_gentest_codegen_prog}
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
        -- ${_gentest_codegen_cross_target_arg}
           ${_gentest_codegen_std_flag}
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME gentest_codegen_check_invalid
    COMMAND ${_gentest_codegen_prog}
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_cross_target_arg}
           ${_gentest_codegen_std_flag}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
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
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_fixture_namespace_scope
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "fixture 'outer_a::inner::SharedFx' is not in an ancestor namespace"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/namespace_scope.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_fixture_attr_removed
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "'fixtures(...)' was removed; infer fixtures from function parameters instead"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/fixtures_attribute_removed.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

# Label codegen/clang-tooling checks so valgrind workflows can exclude them by default.
set_tests_properties(
    gentest_codegen_check_valid
    gentest_codegen_check_invalid
    gentest_codegen_baseline_requires_bench
    gentest_codegen_mock_incomplete_virtual_msg
    gentest_codegen_mock_virtual_defined_in_cpp_msg
    gentest_codegen_mock_anonymous_namespace_msg
    gentest_codegen_mock_local_class_msg
    gentest_codegen_fixture_suite_member_illegal
    gentest_codegen_fixture_global_member_illegal
    gentest_codegen_fixture_namespace_scope
    gentest_codegen_fixture_attr_removed
    PROPERTIES LABELS "codegen")

# End-to-end behavioral checks with robust counting
gentest_add_check_counts(NAME unit_counts PROG $<TARGET_FILE:gentest_unit_tests> PASS 16 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME unit_list_counts PROG $<TARGET_FILE:gentest_unit_tests> LIST CASES 16)

gentest_add_check_counts(NAME integration_counts PROG $<TARGET_FILE:gentest_integration_tests> PASS 3 FAIL 0 SKIP 1)
gentest_add_check_counts(NAME integration_list_counts PROG $<TARGET_FILE:gentest_integration_tests> LIST CASES 4)

gentest_add_check_counts(NAME failing_counts PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 8 SKIP 0)

# The raw failing suite run is expected to fail
set_tests_properties(failing PROPERTIES WILL_FAIL TRUE)

# Verify argument-mismatch message content for mock expectations

gentest_add_check_death(
    NAME failing_free_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    EXPECT_SUBSTRING "fixture allocation failed for 'failing::NullFreeFixture': returned null"
    ARGS --run=failing/alloc/free_null)

gentest_add_check_death(
    NAME failing_suite_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    EXPECT_SUBSTRING "gentest: fixture 'failing::NullSuiteFixture' fixture allocation returned null"
    ARGS --run=failing/alloc/suite_null)

gentest_add_check_death(
    NAME failing_global_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    EXPECT_SUBSTRING "gentest: fixture 'failing::NullGlobalFixture' fixture allocation returned null"
    ARGS --run=failing/alloc/global_null)


gentest_add_check_counts(NAME skiponly_counts PROG $<TARGET_FILE:gentest_skiponly_tests> PASS 0 FAIL 0 SKIP 2)
gentest_add_check_counts(NAME skiponly_list_counts PROG $<TARGET_FILE:gentest_skiponly_tests> LIST CASES 2)

gentest_add_check_counts(NAME fixtures_counts PROG $<TARGET_FILE:gentest_fixtures_tests> PASS 33 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME fixtures_list_counts PROG $<TARGET_FILE:gentest_fixtures_tests> LIST CASES 33)

gentest_add_check_counts(NAME templates_counts PROG $<TARGET_FILE:gentest_templates_tests> PASS 93 FAIL 0 SKIP 0)
gentest_add_check_lines(NAME templates_list_tests_lines PROG $<TARGET_FILE:gentest_templates_tests> LINES 93 ARGS --list-tests)

gentest_add_check_counts(NAME mocking_counts PROG $<TARGET_FILE:gentest_mocking_tests> PASS 26 FAIL 0 SKIP 0)
gentest_add_check_counts(NAME mocking_list_counts PROG $<TARGET_FILE:gentest_mocking_tests> LIST CASES 26)

if(TARGET gentest_coord_tests)
    set(_coord_expected_count 17)
    if(WIN32)
        set(_coord_expected_count 9)
    endif()
    gentest_add_check_counts(NAME coord_counts PROG $<TARGET_FILE:gentest_coord_tests> PASS ${_coord_expected_count} FAIL 0 SKIP 0)
    gentest_add_check_counts(NAME coord_list_counts PROG $<TARGET_FILE:gentest_coord_tests> LIST CASES ${_coord_expected_count})
    unset(_coord_expected_count)
endif()

# Runtime skip/xfail/XPASS outcome checks
set(_gentest_outcomes_runtime_checks
    "outcomes_runtime_skip_simple|0|0|1|0|0|outcomes/runtime_skip_simple"
    "outcomes_runtime_skip_if|0|0|1|0|0|outcomes/runtime_skip_if"
    "outcomes_xfail_expect_fail|0|0|0|1|0|outcomes/xfail_expect_fail"
    "outcomes_xfail_throw|0|0|0|1|0|outcomes/xfail_throw"
    "outcomes_xfail_xpass|0|0|0|0|1|outcomes/xfail_xpass"
    "outcomes_skip_overrides_xfail|0|0|1|0|0|outcomes/skip_overrides_xfail"
    "outcomes_skip_after_failure_is_fail|0|1|0|0|0|outcomes/skip_after_failure_is_fail")

foreach(_gentest_check IN LISTS _gentest_outcomes_runtime_checks)
    string(REPLACE "|" ";" _gentest_fields "${_gentest_check}")
    list(GET _gentest_fields 0 _gentest_name)
    list(GET _gentest_fields 1 _gentest_pass)
    list(GET _gentest_fields 2 _gentest_fail)
    list(GET _gentest_fields 3 _gentest_skip)
    list(GET _gentest_fields 4 _gentest_xfail)
    list(GET _gentest_fields 5 _gentest_xpass)
    list(GET _gentest_fields 6 _gentest_run)

    gentest_add_check_counts(
        NAME ${_gentest_name}
        PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
        PASS ${_gentest_pass}
        FAIL ${_gentest_fail}
        SKIP ${_gentest_skip}
        XFAIL ${_gentest_xfail}
        XPASS ${_gentest_xpass}
        ARGS --run=${_gentest_run})
endforeach()
unset(_gentest_outcomes_runtime_checks)
unset(_gentest_check)
unset(_gentest_fields)
unset(_gentest_name)
unset(_gentest_pass)
unset(_gentest_fail)
unset(_gentest_skip)
unset(_gentest_xfail)
unset(_gentest_xpass)
unset(_gentest_run)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xfail_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml
    EXPECT_SUBSTRING "message=\"xfail: expected failure\""
    ARGS --run=outcomes/xfail_expect_fail --junit=${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xpass_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xpass.xml
    EXPECT_SUBSTRING "xpass: unexpected pass"
    EXPECT_RC 1
    ARGS --run=outcomes/xfail_xpass --junit=${CMAKE_BINARY_DIR}/outcomes_junit_xpass.xml)

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
        ARGS --run=libassert/assert_fail_simple)
endif()

# Verify descriptive predicate mismatch content
gentest_add_check_death(
    NAME mocking_predicate_mismatch_message
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "expected == 3"
    ARGS --run=failing/mocking/predicate_mismatch)

# GitHub annotations smoke: ensure ::error lines present when enabled
gentest_add_check_death(
    NAME github_annotations_smoke
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "::error file="
    ARGS
        --run=failing/mocking/predicate_mismatch
        --github-annotations)

gentest_add_check_death(
    NAME expect_throw_location_smoke
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "EXPECT_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/expect_throw_location)

gentest_add_check_death(
    NAME expect_no_throw_unknown_smoke
    PROG gentest_failing_tests
    EXPECT_SUBSTRING "EXPECT_NO_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/expect_no_throw_unknown)

# CLI checks for VSCode integration
gentest_add_check_contains(NAME unit_help PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING gentest ARGS --help)
gentest_add_check_lines(NAME unit_list_tests_lines PROG $<TARGET_FILE:gentest_unit_tests> LINES 16 ARGS --list-tests)
gentest_add_check_contains(NAME unit_list_tags_fast PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING "tags=fast" ARGS --list)
gentest_add_check_lines(NAME integration_list_tests_lines PROG $<TARGET_FILE:gentest_integration_tests> LINES 4 ARGS --list-tests)
gentest_add_check_contains(NAME integration_list_tags_owner PROG $<TARGET_FILE:gentest_integration_tests> EXPECT_SUBSTRING "owner=team-runtime" ARGS --list)

gentest_add_check_counts(
    NAME run_test_exact
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=unit/arithmetic/sum)

gentest_add_check_counts(
    NAME run_test_default_name_free
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=unit/default_name_free)

gentest_add_check_counts(
    NAME run_test_default_name_member
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=unit/DefaultNameFixture/default_name_member)

gentest_add_check_exit_code(
    NAME run_test_not_found
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_RC 3
    ARGS --run=does/not/exist)

gentest_add_check_counts(
    NAME filter_single
    PROG $<TARGET_FILE:gentest_unit_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --filter=*strings*)

gentest_add_check_lines(NAME fixtures_list_tests_lines PROG $<TARGET_FILE:gentest_fixtures_tests> LINES 33 ARGS --list-tests)

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

gentest_add_check_counts(NAME repeat_unit_twice PROG $<TARGET_FILE:gentest_unit_tests> PASS 32 FAIL 0 SKIP 0 ARGS --repeat=2)

gentest_add_check_counts(NAME failing_fail_fast PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 1 SKIP 0 ARGS --fail-fast)

gentest_add_check_contains(
    NAME shuffle_seed_print
    PROG $<TARGET_FILE:gentest_fixtures_tests>
    EXPECT_SUBSTRING "Shuffle seed: 42"
    ARGS
        --run=fixtures/free/basic
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
            --run=unit/arithmetic/sum
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single)
    set_tests_properties(allure_smoke_single PROPERTIES LABELS "allure")

    # Allure logs attachment: failing test with log_on_fail(true) should create a logs attachment
    gentest_add_run_and_check_file(
        NAME allure_logs_attachment
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-result.json
        EXPECT_SUBSTRING "\"attachments\": [ { \"name\": \"logs\", \"source\": \"result-0-attachment.txt\""
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs)
    set_tests_properties(allure_logs_attachment PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_file
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs/result-0-attachment.txt
        EXPECT_SUBSTRING "hello from log"
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
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
    ARGS --run=ctor/ephemeral_member)

gentest_add_check_death(
    NAME fixture_ctor_free_fixtures
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "unexpected std::exception: free-fx-ctor"
    ARGS --run=ctor/free_fixtures)

gentest_add_check_death(
    NAME fixture_ctor_suite_shared
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "fixture construction threw std::exception: suite-ctor"
    ARGS --run=ctor/suite)

gentest_add_check_death(
    NAME fixture_ctor_global_shared
    PROG ${PROJECT_NAME}_ctor_tests
    EXPECT_SUBSTRING "fixture construction threw std::exception: global-ctor"
    ARGS --run=ctor/global)
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

gentest_add_check_contains(
    NAME benches_filter_space
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING "benchmarks/spacing/lock_guard_small<"
    ARGS --filter benchmarks/spacing/* --kind=bench)

gentest_add_check_death(
    NAME benches_filter_no_match
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING "benchmark filter matched 0 benchmarks"
    ARGS --filter=benchmarks/spacing/nope* --kind=bench)

gentest_add_check_death(
    NAME benches_filter_missing_arg
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING "error: --filter requires a value"
    ARGS --filter)

gentest_add_check_death(
    NAME benches_fixture_alloc_null
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING "benchmark fixture allocation failed for benchmarks/fixture/null"
    ENV_VARS GENTEST_BENCH_NULL_FIXTURE=1
    ARGS --run=benchmarks/fixture/null --kind=bench)

gentest_add_check_exit_code(
    NAME benches_fixture_free_suite_global
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global --kind=bench)

gentest_add_check_death(
    NAME jitter_fixture_alloc_null
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_SUBSTRING "jitter fixture allocation failed for benchmarks/fixture/jitter_null"
    ENV_VARS GENTEST_JITTER_NULL_FIXTURE=1
    ARGS --run=benchmarks/fixture/jitter_null --kind=jitter)

gentest_add_check_exit_code(
    NAME jitter_fixture_free_suite_global
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global_jitter --kind=jitter)

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

gentest_add_check_death(
    NAME cli_unknown_option
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "error: unknown option"
    ARGS --this-option-does-not-exist)

gentest_add_check_death(
    NAME cli_unknown_option_hard_fail
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "error: unknown option '--this-option-does-not-exist'"
    ARGS --run=unit/arithmetic/sum --this-option-does-not-exist)

gentest_add_check_death(
    NAME cli_removed_run_test
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "error: --run-test was removed; use --run"
    ARGS --run-test=unit/arithmetic/sum)

gentest_add_check_contains(
    NAME cli_kind_filter_zero_bench
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "Executed 0 benchmark(s)."
    ARGS --kind=bench)

gentest_add_check_death(
    NAME cli_bench_table_invalid_kind
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "error: --bench-table requires --kind=bench or --kind=all"
    ARGS --bench-table --kind=jitter)

function(_gentest_add_cmake_helper_test name source_dir script_path label)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${source_dir}
            -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DGENERATOR=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            ${ARGN}
            -P ${script_path})

    set_tests_properties(${name} PROPERTIES LABELS "${label}")
endfunction()

_gentest_add_cmake_helper_test(
    gentest_codegen_output_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_collision
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenOutputCollision.cmake
    cmake)

set(_gentest_discover_tests_config "")
if(CMAKE_CONFIGURATION_TYPES)
    set(_gentest_discover_tests_config "Debug")
endif()

_gentest_add_cmake_helper_test(
    gentest_discover_tests_smoke
    ${PROJECT_SOURCE_DIR}/tests/cmake/discover_tests
    ${PROJECT_SOURCE_DIR}/cmake/CheckDiscoverTests.cmake
    cmake
    -DBUILD_CONFIG=${_gentest_discover_tests_config})

unset(_gentest_discover_tests_config)

_gentest_add_cmake_helper_test(
    gentest_tu_wrapper_source_props
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_wrapper_source_props
    ${PROJECT_SOURCE_DIR}/cmake/CheckTuWrapperSourceProps.cmake
    cmake)

option(GENTEST_ENABLE_PACKAGE_TESTS "Enable install + find_package consumer smoke test (FetchContent)" OFF)
if(GENTEST_ENABLE_PACKAGE_TESTS)
    set(_gentest_package_test_config "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_gentest_package_test_config "Debug")
    endif()

    _gentest_add_cmake_helper_test(
        gentest_package_consumer
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config})
endif()

# Local dev workflow: run test suite under valgrind via CTest's MemCheck step.
#
# Notes:
# - This is *much* slower than running plain tests.
# - We exclude some tests by label by default to keep runtime reasonable (override via cache var).
find_program(GENTEST_VALGRIND_COMMAND valgrind)
if(GENTEST_VALGRIND_COMMAND)
    set(GENTEST_VALGRIND_LABEL_EXCLUDE_REGEX "cmake|package|codegen" CACHE STRING
        "CTest label exclude regex for gentest_valgrind (ctest -LE <regex>). Set empty to include everything.")

    set(_gentest_valgrind_cmd ${CMAKE_CTEST_COMMAND} -T MemCheck --output-on-failure)
    if(NOT GENTEST_VALGRIND_LABEL_EXCLUDE_REGEX STREQUAL "")
        # Escape pipes for the build-tool shell so ctest receives 'cmake|foo|bar' rather than interpreting as pipelines.
        string(REPLACE "|" "\\|" _gentest_valgrind_label_exclude_shell "${GENTEST_VALGRIND_LABEL_EXCLUDE_REGEX}")
        list(APPEND _gentest_valgrind_cmd -LE "${_gentest_valgrind_label_exclude_shell}")
    endif()

    add_custom_target(gentest_valgrind
        COMMAND ${_gentest_valgrind_cmd}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL)

    add_dependencies(gentest_valgrind gentest_test_suites)
    if(TARGET gentest_core_tests)
        add_dependencies(gentest_valgrind gentest_core_tests)
    endif()
    if(TARGET gentest_codegen)
        add_dependencies(gentest_valgrind gentest_codegen)
    endif()

    unset(_gentest_valgrind_cmd)
    unset(_gentest_valgrind_label_exclude_shell)
endif()
