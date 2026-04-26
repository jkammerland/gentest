include(GentestTests)
include("${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleFixtureCommon.cmake")

if(NOT TARGET gentest_test_suites)
    add_custom_target(gentest_test_suites)
endif()

add_subdirectory(tool_core)

gentest_resolve_test_std(_gentest_test_std_feature _gentest_codegen_std_flag)
unset(_gentest_test_std_feature)
gentest_normalize_std_flag_for_compiler(_gentest_codegen_scan_std_flag "clang++" "${_gentest_codegen_std_flag}")

set(_gentest_default_suites
    unit
    integration
    failing
    skiponly
    fixtures
    readme_fixtures
    templates
    mocking
    ctor
    benchmarks)

set(_gentest_test_suites ${_gentest_default_suites})

# Optionally include libassert integration suite
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_test_suites libassert)
endif()

foreach(suite IN LISTS _gentest_test_suites)
    gentest_add_suite(${suite})
endforeach()

unset(_gentest_test_suites)

gentest_add_mocks(gentest_textual_suite_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/explicit_textual_mocks.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/explicit_textual_suite_mocks"
    HEADER_NAME public/gentest_textual_suite_mocks.hpp)

gentest_link_mocks(gentest_mocking_tests PRIVATE gentest_textual_suite_mocks)
gentest_link_mocks(gentest_failing_tests PRIVATE gentest_textual_suite_mocks)
if(TARGET ${PROJECT_NAME}_libassert_tests)
    gentest_link_mocks(${PROJECT_NAME}_libassert_tests PRIVATE gentest_textual_suite_mocks)
endif()

# Concurrency target (no default add_test to avoid suite-wide failure)
gentest_add_suite(concurrency TARGET ${PROJECT_NAME}_concurrency_tests NO_CTEST)
gentest_link_mocks(${PROJECT_NAME}_concurrency_tests PRIVATE gentest_textual_suite_mocks)

# Outcome semantics target (no default add_test; contains deliberate FAIL/XPASS cases)
gentest_add_suite(outcomes TARGET ${PROJECT_NAME}_outcomes_tests NO_CTEST)

# Coroutine async target (no default add_test; contains deliberate BLOCKED cases)
gentest_add_suite(async TARGET ${PROJECT_NAME}_async_tests NO_CTEST)

# Slow coroutine live-renderer demo target (no default suite add_test; intended for terminal inspection)
gentest_add_suite(async_live_slow TARGET ${PROJECT_NAME}_async_live_slow_tests NO_CTEST)

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
set(_gentest_codegen_parallel_bench_suites
    ${_gentest_default_suites}
    concurrency
    outcomes
    async)
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_codegen_parallel_bench_suites libassert)
endif()

set(_gentest_codegen_parallel_bench_sources "")
foreach(suite IN LISTS _gentest_codegen_parallel_bench_suites)
    list(APPEND _gentest_codegen_parallel_bench_sources ${CMAKE_CURRENT_SOURCE_DIR}/${suite}/cases.cpp)
endforeach()

add_library(gentest_codegen_parallel_bench_obj OBJECT EXCLUDE_FROM_ALL ${_gentest_codegen_parallel_bench_sources})

target_link_libraries(gentest_codegen_parallel_bench_obj PRIVATE gentest_main)
gentest_link_mocks(gentest_codegen_parallel_bench_obj PRIVATE gentest_textual_suite_mocks)

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

unset(_gentest_codegen_parallel_bench_sources)
unset(_gentest_codegen_parallel_bench_suites)
unset(_gentest_default_suites)

# Focused checks: ensure pass-only selection passes, and failing selection returns non-zero
set(_gentest_concurrency_count_checks
    "concurrency_pass_single|1|0|0|concurrency/child_expect_pass"
    "concurrency_pass_throw_single|1|0|0|concurrency/child_expect_throw_pass"
    "concurrency_multi_pass_single|1|0|0|concurrency/multi_adopt_expect_pass"
    "concurrency_mock_dispatch_single|1|0|0|concurrency/mock_adopt_dispatch"
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
    "concurrency_fail_single_death|concurrency/child_expect_fail|gentest: fatal: assertion/expectation recorded without an active test context."
    "concurrency_skip_no_context_death|concurrency/child_skip_no_context|gentest: fatal: skip called without an active test context."
    "concurrency_xfail_no_context_death|concurrency/child_xfail_no_context|gentest: fatal: xfail called without an active test context."
    "concurrency_multi_noadopt_death|concurrency/no_adopt_expect_death_multi|gentest: fatal: assertion/expectation recorded without an active test context.")

foreach(_gentest_check IN LISTS _gentest_concurrency_death_checks)
    string(REPLACE "|" ";" _gentest_fields "${_gentest_check}")
    list(GET _gentest_fields 0 _gentest_name)
    list(GET _gentest_fields 1 _gentest_run)
    list(GET _gentest_fields 2 _gentest_required_substring)

    gentest_add_check_death(
        NAME ${_gentest_name}
        PROG ${PROJECT_NAME}_concurrency_tests
        REQUIRED_SUBSTRINGS
            "${_gentest_required_substring}"
            "Did you forget to set the current context in this thread/coroutine?"
        ARGS --run=${_gentest_run})
endforeach()
unset(_gentest_concurrency_death_checks)
unset(_gentest_check)
unset(_gentest_fields)
unset(_gentest_name)
unset(_gentest_required_substring)
unset(_gentest_run)

gentest_add_check_counts(
    NAME async_batch_cooperative
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 2
    FAIL 0
    SKIP 0
    ARGS --filter=async/batch/*)

gentest_add_cmake_script_test(
    NAME async_live_renderer_non_tty_plain
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --filter=async/batch/* --kind=test
    DEFINES
        "EXPECT_RC=0"
        "FORBID_SUBSTRINGS=RUNNING|SUSPENDED")

gentest_add_check_counts(
    NAME async_live_demo_cooperative
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 2
    FAIL 0
    SKIP 0
    ARGS --filter=async/live_demo/* --kind=test --no-color)

gentest_add_cmake_script_test(
    NAME async_fail_fast_stops_after_async_failure
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --filter=async/fail_fast/* --kind=test --fail-fast --no-color
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=[ FAIL ] async/fail_fast/00_async_fail"
        "FORBID_SUBSTRING=async/fail_fast/01_sync_should_not_run")

gentest_add_check_counts(
    NAME async_live_slow_demo_inventory
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_live_slow_tests>
    LIST
    CASES 6
    ARGS --kind=test)

gentest_add_check_counts(
    NAME async_value_discard
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=async/value/discard)

gentest_add_check_counts(
    NAME async_local_fixture
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=async/fixture/local_async)

gentest_add_check_counts(
    NAME async_mixed_fixtures
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=async/fixture/mixed)

gentest_add_check_counts(
    NAME async_unresumable_failure
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 0
    FAIL 1
    SKIP 0
    BLOCKED 0
    EXPECT_RC 1
    ARGS --run=async/blocked/never)

gentest_add_cmake_script_test(
    NAME async_unresumable_failure_reason
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=async/blocked/never --kind=test --no-color
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=cannot resume, resume handle never created or lost"
        "FORBID_SUBSTRING=[ BLOCKED ] async/blocked/never")

gentest_add_cmake_script_test(
    NAME async_unresumable_failure_location
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=async/blocked/never --kind=test --no-color
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=last suspended at tests/async/cases.cpp:"
        "FORBID_SUBSTRING=[ BLOCKED ] async/blocked/never")

gentest_add_check_counts(
    NAME async_selected_dependency_unresumable
    PROG $<TARGET_FILE:${PROJECT_NAME}_async_tests>
    PASS 0
    FAIL 1
    SKIP 0
    BLOCKED 0
    EXPECT_RC 1
    ARGS --run=async/batch/client)

option(GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS "Skip Windows Debug death tests that hang on CI" OFF)
if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
    set_tests_properties(
        concurrency_fail_single_death concurrency_skip_no_context_death concurrency_xfail_no_context_death concurrency_multi_noadopt_death
        PROPERTIES DISABLED "$<CONFIG:Debug>")
endif()

option(GENTEST_SKIP_TSAN_DEATH_TESTS "Skip death tests whose stderr contract is incompatible with ThreadSanitizer" OFF)
if(GENTEST_SKIP_TSAN_DEATH_TESTS)
    set_tests_properties(
        concurrency_fail_single_death concurrency_skip_no_context_death concurrency_xfail_no_context_death concurrency_multi_noadopt_death
        PROPERTIES DISABLED TRUE)
endif()

option(GENTEST_ENABLE_TSAN_STRESS_TESTS "Enable long-running TSAN stress reproducers" OFF)
set(GENTEST_TSAN_STRESS_TIMEOUT_SEC "660" CACHE STRING "Timeout in seconds for TSAN stress reproducers")

set(_gentest_is_tsan_build OFF)
gentest_detect_thread_sanitizer(_gentest_is_tsan_build)

if(NOT GENTEST_CODEGEN_EXECUTABLE AND NOT GENTEST_CODEGEN_TARGET AND NOT TARGET gentest_codegen)
    message(FATAL_ERROR
        "tests: no gentest code generator available (gentest_codegen). "
        "Configure with -DGENTEST_BUILD_CODEGEN=ON or -DGENTEST_CODEGEN_EXECUTABLE=<path>.")
endif()

_gentest_resolve_codegen_backend(
    TARGET tests
    OUT_CODEGEN_TARGET _gentest_codegen_target
    OUT_CODEGEN_EXECUTABLE _gentest_codegen_prog)

_gentest_collect_cross_codegen_clang_args(_gentest_codegen_cross_target_arg)
set(_gentest_codegen_public_include_args
    -I${PROJECT_SOURCE_DIR}/include
    -I${CMAKE_CURRENT_SOURCE_DIR})
gentest_append_public_dependency_include_args(_gentest_codegen_public_include_args "${CMAKE_BINARY_DIR}")
_gentest_collect_common_codegen_system_include_args(_gentest_codegen_system_include_args)
set(_gentest_codegen_direct_clang_args
    ${_gentest_codegen_cross_target_arg}
    ${_gentest_codegen_scan_std_flag}
    ${_gentest_codegen_public_include_args}
    ${_gentest_codegen_system_include_args})
set(_gentest_codegen_direct_cxx_language_clang_args
    ${_gentest_codegen_cross_target_arg}
    -x
    c++
    ${_gentest_codegen_scan_std_flag}
    ${_gentest_codegen_public_include_args}
    ${_gentest_codegen_system_include_args})

function(_gentest_add_proof_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE gentest_runtime)
    target_compile_features(${name} PRIVATE cxx_std_20)
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
    set_property(TEST ${name} APPEND PROPERTY LABELS "proof")
endfunction()

_gentest_add_proof_test(
    gentest_proof_api_context_adopt_copy_contract
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/adopt_copy_contract_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_context_event_chronology
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/event_chronology_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_context_always_log
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/always_log_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_context_log_policy
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/log_policy_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_context_expect_throw_std_exception
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/expect_throw_std_exception_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_context_header_isolation
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/header_isolation_red.cpp)
_gentest_add_proof_test(
    gentest_proof_api_assertions_header_isolation
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/api_context/assertions_header_isolation_red.cpp)
add_executable(
    gentest_proof_measured_call_phase_body_timing
    ${CMAKE_CURRENT_SOURCE_DIR}/proofs/measured/call_phase_body_timing_red.cpp)
target_include_directories(
    gentest_proof_measured_call_phase_body_timing
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gentest_proof_measured_call_phase_body_timing PRIVATE gentest_runtime)
target_compile_features(gentest_proof_measured_call_phase_body_timing PRIVATE cxx_std_20)
add_test(NAME gentest_proof_measured_call_phase_body_timing COMMAND $<TARGET_FILE:gentest_proof_measured_call_phase_body_timing>)
set_property(TEST gentest_proof_measured_call_phase_body_timing APPEND PROPERTY LABELS "proof")
gentest_add_cmake_script_test(
    NAME gentest_runner_registry_runtime_hidden
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckRunnerRegistryRuntimeHidden.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/runner_registry_runtime_hidden_check"
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}")
set_property(TEST gentest_runner_registry_runtime_hidden APPEND PROPERTY LABELS "proof")

gentest_add_cmake_script_test(
    NAME gentest_vendored_indicators_compile
    NO_EMULATOR
    PROG ${CMAKE_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckVendoredIndicatorsCompile.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/vendored_indicators_compile_check"
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}")

# Lint-only checks for generator
gentest_add_check_exit_code(
    NAME gentest_codegen_check_valid
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_namespaced_attrs
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_axis_generators
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_axis_generators.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_parameters_pack
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_parameters_pack.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_fixture_resolution
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_fixture_resolution.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_template_fixture_resolution
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_template_fixture_resolution.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_template_parser_edges
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_template_parser_edges.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_type_quoting_edges
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/codegen_type_quoting.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_cmake_script_test(
    NAME gentest_codegen_warns_unknown_preprocessor_condition
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_unknown_condition_warn.cppm
        -- ${_gentest_codegen_direct_clang_args}
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan at ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_unknown_condition_warn.cppm:6"
        "EXPECT_COUNT_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan"
        "EXPECT_COUNT=1")

gentest_add_cmake_script_test(
    NAME gentest_codegen_active_elif_unknown_warn
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_active_elif_unknown_warn.cppm
        -- ${_gentest_codegen_direct_clang_args}
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan at ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_active_elif_unknown_warn.cppm:7"
        "EXPECT_COUNT_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan"
        "EXPECT_COUNT=1")

gentest_add_cmake_script_test(
    NAME gentest_codegen_supported_preprocessor_condition_no_warning
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_supported_condition_nowarn.cppm
        -- ${_gentest_codegen_direct_clang_args}
    DEFINES
        "EXPECT_RC=0"
        "FORBID_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan")

gentest_add_cmake_script_test(
    NAME gentest_codegen_dead_elif_unknown_no_warning
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/scan_dead_elif_unknown_nowarn.cppm
        -- ${_gentest_codegen_direct_clang_args}
    DEFINES
        "EXPECT_RC=0"
        "FORBID_SUBSTRING=warning: unable to evaluate preprocessor condition during module/import scan")

gentest_add_cmake_script_test(
    NAME gentest_codegen_compile_command_condition_warnings
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenCompileCommandConditionWarnings.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tests/gentest_codegen_compile_command_condition_warnings"
        "GENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}")

gentest_add_cmake_script_test(
    NAME gentest_find_supported_ninja_portability
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFindSupportedNinjaPortability.cmake")

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_keyword
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "attribute 'death' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_value_keyword
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "attribute 'req(\"REQ-MIS-SCOPED\")' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_unqualified_test
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "attribute 'test(\"smoke/mis_scoped/plain\")' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_mixed_std_first
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
    ARGS
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_std_first
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_std_first/namespaced_attrs_mixed_std_first.gentest.h
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_std_first/namespaced_attrs_mixed_std_first.gentest.h"
        "REQUIRED_SUBSTRING=smoke/namespaced/mixed/std_first")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_mixed_scoped_first
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
    ARGS
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_scoped_first
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_scoped_first/namespaced_attrs_mixed_scoped_first.gentest.h
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_scoped_first/namespaced_attrs_mixed_scoped_first.gentest.h"
        "REQUIRED_SUBSTRING=smoke/namespaced/mixed/scoped_first")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_gentest_first_foreign_after
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
    ARGS
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_foreign_after
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_foreign_after/namespaced_attrs_gentest_first_foreign_after.gentest.h
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_foreign_after/namespaced_attrs_gentest_first_foreign_after.gentest.h"
        "REQUIRED_SUBSTRING=smoke/namespaced/mixed/gentest_first_foreign_after")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_gentest_first_unscoped_foreign
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
    ARGS
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_unscoped_foreign
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_unscoped_foreign/namespaced_attrs_gentest_first_unscoped_foreign.gentest.h
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_unscoped_foreign/namespaced_attrs_gentest_first_unscoped_foreign.gentest.h"
        "REQUIRED_SUBSTRING=smoke/namespaced/mixed/gentest_first_unscoped_foreign")

gentest_add_cmake_script_test(
    NAME gentest_codegen_namespace_suite_attr_with_trailing_comment
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
    ARGS
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/namespace_suite_comment
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/namespace_suite_comment/namespace_suite_comment.gentest.h
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespace_suite_comment.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespace_suite_comment/namespace_suite_comment.gentest.h"
        "REQUIRED_SUBSTRING=suite_override/comment_backscan/trailing_comment_suite_attr_case")

gentest_add_cmake_script_test(
    NAME gentest_codegen_namespace_suite_diag_dedup
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespace_suite_diag_dedup.cpp
        --
        ${_gentest_codegen_direct_clang_args}
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=duplicate gentest namespace attribute 'suite'"
        "EXPECT_COUNT_SUBSTRING=attribute 'suite(\"smoke/namespace_diag/plain\")' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
        "EXPECT_COUNT=1")

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_unknown_attr
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "unknown gentest attribute 'unknown' (will_fail)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_duplicate_template_attr
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "duplicate 'template' attribute for the same parameter (duplicate_template_attr)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_template_pack_bare
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "template parameter pack 'Cs' requires parenthesized rows in 'template(Cs, ...)' (template_pack_bare)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_template_nonpack_row
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "non-pack template parameter 'C' does not accept parenthesized rows in 'template(C, ...)' (template_nonpack_row)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_template_parenthesized_nttp_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/template_parenthesized_nttp_ok.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_invalid_template_pack_empty_cells
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 1
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_template_pack_empty_cells.cpp
        -- ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_baseline_requires_bench
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "'baseline' requires 'bench' or 'jitter'"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/baseline_requires_bench.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_unknown_parameters_name
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "unknown parameter name 'missing' in parameters(...)"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_discovery.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_unknown_pack_name
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "unknown parameter name 'missing' in parameters_pack(...)"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_discovery.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_duplicate_axis
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "duplicate parameter axis for 'i'"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_discovery.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_overlap_pack
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "parameter 'i' supplied by multiple parameter sets/packs"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_discovery.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_range_inconsistent
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "parameters_range for 'i' has inconsistent bounds"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_discovery.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_range_unsupported
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "parameters_range not supported for parameter type"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_range_unsupported.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_linspace_unsupported
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "parameters_linspace not supported for parameter type"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_linspace_unsupported.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_geom_unsupported
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "parameters_geom not supported for parameter type"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_geom_unsupported.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_invalid_axis_logspace_unsupported
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "logspace not supported for parameter type"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_axis_logspace_unsupported.cpp
        --
        ${_gentest_codegen_direct_clang_args})

# Generator negative tests for mocks: robust diagnostics
set(_gentest_codegen_discover_mocks_arg --discover-mocks)

gentest_add_check_death(
    NAME gentest_codegen_mock_incomplete_virtual_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "gentest::mock<T>: target type is incomplete"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/incomplete_virtual_ref.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_virtual_defined_in_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "target definition must be in a header or header module"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/virtual_defined_in_cpp.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "target definition must be in a header or header module"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_cpp.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_header_main_tu_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/header_main_tu.hpp
        --
        ${_gentest_codegen_direct_cxx_language_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_cu_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "target definition must be in a header or header module"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_cu.cu
        --
        ${_gentest_codegen_direct_cxx_language_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "target definition must be in a header or header module"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cpp.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cppm_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "target definition must be in a header or header module"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cppm.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cu_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 1
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cu.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_noext_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_noext.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_mpp_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_mpp.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_emit_header_main_tu_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --tu-out-dir
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu
        --tu-header-output
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu/mock_emit_header_main_tu.gentest.h
        --mock-registry
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_registry.hpp
        --mock-impl
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_impl.hpp
        --mock-domain-registry-output
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_registry__domain_0000_header.hpp
        --mock-domain-impl-output
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_impl__domain_0000_header.hpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/header_main_tu.hpp
        --
        ${_gentest_codegen_direct_cxx_language_clang_args})

if(WIN32)
    gentest_add_cmake_script_test(
        NAME gentest_codegen_mock_cross_root_include_windows
        NO_EMULATOR
        PROG ${_gentest_codegen_prog}
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMockCrossRootInclude.cmake"
        DEFINES
            "BUILD_ROOT=${CMAKE_BINARY_DIR}"
            "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "TESTS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "CODEGEN_STD=${_gentest_codegen_scan_std_flag}"
            "CROSS_TARGET_ARG=${_gentest_codegen_cross_target_arg}")
endif()

gentest_add_cmake_script_test(
    NAME gentest_codegen_mock_symlink_include
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMockSymlinkInclude.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}"
        "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "CODEGEN_STD=${_gentest_codegen_scan_std_flag}"
        "CROSS_TARGET_ARG=${_gentest_codegen_cross_target_arg}")

gentest_add_check_death(
    NAME gentest_codegen_mock_anonymous_namespace_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "cannot mock a type in an anonymous namespace"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/anonymous_namespace_type.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_mock_local_class_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "cannot mock a local class"
    ARGS
        ${_gentest_codegen_discover_mocks_arg}
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/local_class.cpp
        --
        ${_gentest_codegen_direct_clang_args})
unset(_gentest_codegen_discover_mocks_arg)

# Generator negative tests for fixtures: disallow member tests on suite/global under strict mode
gentest_add_check_death(
    NAME gentest_codegen_fixture_suite_member_illegal
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "suite fixtures cannot declare member tests"
    ENV_VARS GENTEST_STRICT_FIXTURE=1
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/suite_member_test.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_fixture_global_member_illegal
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "global fixtures cannot declare member tests"
    ENV_VARS GENTEST_STRICT_FIXTURE=1
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/global_member_test.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_fixture_namespace_scope
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "fixture 'outer_a::inner::SharedFx' is not in an ancestor namespace"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/namespace_scope.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_top_level_suite_fixture
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "'fixture(suite)' requires an enclosing named namespace to derive a suite path"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/top_level_suite_fixture.cpp
        --
        ${_gentest_codegen_direct_clang_args})

gentest_add_check_death(
    NAME gentest_codegen_fixture_attr_removed
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    REQUIRED_SUBSTRING "'fixtures(...)' was removed; infer fixtures from function parameters instead"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixture_errors/fixtures_attribute_removed.cpp
        --
        ${_gentest_codegen_direct_clang_args})

# Label codegen/clang-tooling checks so valgrind workflows can exclude them by default.
set_tests_properties(
    gentest_codegen_check_valid
    gentest_codegen_check_namespaced_attrs
    gentest_codegen_check_axis_generators
    gentest_codegen_check_parameters_pack
    gentest_codegen_check_template_fixture_resolution
    gentest_codegen_mis_scoped_attr_keyword
    gentest_codegen_mis_scoped_attr_value_keyword
    gentest_codegen_mis_scoped_attr_unqualified_test
    gentest_codegen_namespace_suite_attr_with_trailing_comment
    gentest_codegen_namespace_suite_diag_dedup
    gentest_codegen_check_invalid_unknown_attr
    gentest_codegen_check_invalid_duplicate_template_attr
    gentest_codegen_check_invalid_template_pack_bare
    gentest_codegen_check_invalid_template_nonpack_row
    gentest_codegen_check_template_parenthesized_nttp_ok
    gentest_codegen_check_invalid_template_pack_empty_cells
    gentest_codegen_baseline_requires_bench
    gentest_codegen_invalid_axis_unknown_parameters_name
    gentest_codegen_invalid_axis_unknown_pack_name
    gentest_codegen_invalid_axis_duplicate_axis
    gentest_codegen_invalid_axis_overlap_pack
    gentest_codegen_invalid_axis_range_inconsistent
    gentest_codegen_invalid_axis_range_unsupported
    gentest_codegen_invalid_axis_linspace_unsupported
    gentest_codegen_invalid_axis_geom_unsupported
    gentest_codegen_invalid_axis_logspace_unsupported
    gentest_codegen_mock_incomplete_virtual_msg
    gentest_codegen_mock_virtual_defined_in_cpp_msg
    gentest_codegen_mock_nonvirtual_defined_in_cpp_msg
    gentest_codegen_mock_header_main_tu_ok
    gentest_codegen_mock_nonvirtual_defined_in_cu_msg
    gentest_codegen_mock_nonvirtual_defined_in_included_cpp_msg
    gentest_codegen_mock_nonvirtual_defined_in_included_cppm_msg
    gentest_codegen_mock_nonvirtual_defined_in_included_cu_msg
    gentest_codegen_mock_nonvirtual_defined_in_included_noext_ok
    gentest_codegen_mock_nonvirtual_defined_in_included_mpp_ok
    gentest_codegen_mock_emit_header_main_tu_ok
    gentest_codegen_mock_anonymous_namespace_msg
    gentest_codegen_mock_local_class_msg
    gentest_codegen_fixture_suite_member_illegal
    gentest_codegen_fixture_global_member_illegal
    gentest_codegen_fixture_namespace_scope
    gentest_codegen_top_level_suite_fixture
    gentest_codegen_fixture_attr_removed
    PROPERTIES LABELS "codegen")

if(WIN32)
    set_property(TEST gentest_codegen_mock_cross_root_include_windows APPEND PROPERTY LABELS "codegen")
endif()
set_property(TEST gentest_codegen_mock_symlink_include APPEND PROPERTY LABELS "codegen")

# End-to-end behavioral checks with structured suite inventory
set(_gentest_suite_inventory_checks
    "unit|gentest_unit_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/unit.list"
    "integration|gentest_integration_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/integration.list"
    "skiponly|gentest_skiponly_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/skiponly.list"
    "fixtures|gentest_fixtures_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/fixtures.list"
    "readme_fixtures|gentest_readme_fixtures_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/readme_fixtures.list"
    "templates|gentest_templates_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/templates.list"
    "mocking|gentest_mocking_tests|${CMAKE_CURRENT_SOURCE_DIR}/expected/mocking.list")

foreach(_gentest_inventory IN LISTS _gentest_suite_inventory_checks)
    string(REPLACE "|" ";" _gentest_inventory_fields "${_gentest_inventory}")
    list(GET _gentest_inventory_fields 0 _gentest_inventory_name)
    list(GET _gentest_inventory_fields 1 _gentest_inventory_prog)
    list(GET _gentest_inventory_fields 2 _gentest_inventory_expected_list)

    gentest_add_check_inventory(
        NAME ${_gentest_inventory_name}_inventory
        PROG ${_gentest_inventory_prog}
        EXPECTED_LIST_FILE ${_gentest_inventory_expected_list})
endforeach()
unset(_gentest_suite_inventory_checks)
unset(_gentest_inventory)
unset(_gentest_inventory_fields)
unset(_gentest_inventory_name)
unset(_gentest_inventory_prog)
unset(_gentest_inventory_expected_list)

gentest_add_check_inventory(
    NAME unit_inventory_legacy_expected_list
    PROG gentest_unit_tests
    EXPECTED_LIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/expected/unit_legacy_plain.list)

gentest_add_check_inventory(
    NAME outcomes_inventory_status_markers
    PROG gentest_outcomes_tests
    EXPECTED_LIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/expected/outcomes_status.list)

gentest_add_check_inventory(
    NAME inventory_parser_semicolon_case_name
    PROG ${CMAKE_COMMAND}
    ARGS -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/FakeInventoryProgram.cmake --
    EXPECTED_LIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/expected/fake_inventory_semicolon_status.list)

gentest_add_check_inventory(
    NAME inventory_parser_blocked_case_name
    PROG ${CMAKE_COMMAND}
    ARGS -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/FakeBlockedInventoryProgram.cmake --
    EXPECTED_LIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/expected/fake_inventory_blocked_status.list)

gentest_add_check_counts(NAME failing_counts PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 10 SKIP 0 BLOCKED 2)

# The raw failing suite run is expected to fail
set_tests_properties(failing PROPERTIES WILL_FAIL TRUE)

# Verify argument-mismatch message content for mock expectations

gentest_add_check_death(
    NAME failing_free_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    REQUIRED_SUBSTRING "fixture allocation failed for 'failing::NullFreeFixture': returned null"
    ARGS --run=failing/alloc/free_null)

gentest_add_check_death(
    NAME failing_suite_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    REQUIRED_SUBSTRING "fixture 'failing::NullSuiteFixture' fixture allocation returned null"
    ARGS --run=failing/alloc/suite_null)

gentest_add_check_death(
    NAME failing_global_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    REQUIRED_SUBSTRING "fixture 'failing::NullGlobalFixture' fixture allocation returned null"
    ARGS --run=failing/alloc/global_null)


# Runtime skip/xfail/XPASS outcome checks
set(_gentest_outcomes_runtime_checks
    "outcomes_runtime_skip_simple|0|0|1|0|0|outcomes/runtime_skip_simple"
    "outcomes_runtime_skip_if|0|0|1|0|0|outcomes/runtime_skip_if"
    "outcomes_runtime_skip_prefix_user_text|0|0|1|0|0|outcomes/runtime_skip_prefix_user_text"
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
    NAME outcomes_junit_runtime_skip_prefix_user_text
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_runtime_skip_prefix.xml
    REQUIRED_SUBSTRING "failures=\"0\""
    ARGS --run=outcomes/runtime_skip_prefix_user_text --junit=${CMAKE_BINARY_DIR}/outcomes_junit_runtime_skip_prefix.xml)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xfail_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml
    REQUIRED_SUBSTRING "message=\"xfail: expected failure\""
    ARGS --run=outcomes/xfail_expect_fail --junit=${CMAKE_BINARY_DIR}/outcomes_junit_xfail.xml)

gentest_add_run_and_check_file(
    NAME outcomes_junit_xpass_mapping
    PROG $<TARGET_FILE:${PROJECT_NAME}_outcomes_tests>
    FILE ${CMAKE_BINARY_DIR}/outcomes_junit_xpass.xml
    REQUIRED_SUBSTRING "xpass: unexpected pass"
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
        PASS 7
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
        REQUIRED_SUBSTRING "gentest: exceptions are disabled; terminating after fatal assertion"
        ARGS --run=libassert/assert_fail_simple)
endif()

# Verify descriptive predicate mismatch content
gentest_add_check_death(
    NAME mocking_predicate_mismatch_message
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "expected == 3"
    ARGS --run=failing/mocking/predicate_mismatch)

gentest_add_check_death(
    NAME check_death_required_substrings_preserves_list
    PROG gentest_failing_tests
    REQUIRED_SUBSTRINGS
        "failing/mocking/predicate_mismatch"
        "argument[0] mismatch for ::mocking::Ticker::tick: expected == 3, got 4"
    ARGS --run=failing/mocking/predicate_mismatch)

if(WIN32 AND (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    gentest_add_check_death(
        NAME mocking_ambiguous_method_pointer_message
        PROG gentest_failing_tests
        REQUIRED_SUBSTRING "ambiguous mock method pointer for ::mocking::Ticker"
        ARGS --run=failing/mocking/ambiguous_template_member_pointer)
endif()

# GitHub annotations smoke: ensure ::error lines present when enabled
gentest_add_check_death(
    NAME github_annotations_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "::error file="
    ARGS
        --run=failing/mocking/predicate_mismatch
        --github-annotations)

gentest_add_check_death(
    NAME expect_throw_location_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "EXPECT_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/expect_throw_location)

gentest_add_check_death(
    NAME expect_no_throw_unknown_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "EXPECT_NO_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/expect_no_throw_unknown)

gentest_add_check_death(
    NAME assert_throw_location_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "ASSERT_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/assert_throw_location)

gentest_add_check_death(
    NAME assert_no_throw_unknown_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "ASSERT_NO_THROW failed at tests/failing/cases.cpp:"
    ARGS --run=failing/exceptions/assert_no_throw_unknown)

gentest_add_check_death(
    NAME expect_eq_message_values_smoke
    PROG gentest_failing_tests
    REQUIRED_SUBSTRING "comparison detail; lhs=1, rhs=2"
    ARGS --run=failing/comparison/expect_eq_message_values)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Regressions.cmake)

gentest_add_manual_regression(
    TARGET gentest_regression_measured_report_coverage
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_report_coverage.cpp)

gentest_add_check_exit_code(
    NAME regression_measured_report_coverage
    PROG $<TARGET_FILE:gentest_regression_measured_report_coverage>
    EXPECT_RC 0)

gentest_add_manual_regression(
    TARGET gentest_regression_async_status_renderer
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/regressions/async_status_renderer.cpp)

gentest_add_check_exit_code(
    NAME regression_async_status_renderer
    PROG $<TARGET_FILE:gentest_regression_async_status_renderer>
    EXPECT_RC 0)

# CLI checks for VSCode integration
gentest_add_check_contains(NAME unit_help PROG $<TARGET_FILE:gentest_unit_tests> REQUIRED_SUBSTRING gentest ARGS --help)
gentest_add_check_contains(NAME unit_help_time_unit PROG $<TARGET_FILE:gentest_unit_tests> REQUIRED_SUBSTRING "--time-unit=<mode>" ARGS --help)
gentest_add_check_contains(NAME unit_list_contains_attr_close_marker_name_prefix PROG $<TARGET_FILE:gentest_unit_tests> REQUIRED_SUBSTRING "unit/attributes/close_marker_in_string_" ARGS --list-tests)
gentest_add_check_contains(NAME unit_list_tags_fast PROG $<TARGET_FILE:gentest_unit_tests> REQUIRED_SUBSTRING "tags=fast" ARGS --list)
gentest_add_check_contains(
    NAME unit_list_tags_requires_shape
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "strings/concatenate [gentest:tags=slow;requires=#42]"
    ARGS --list)
gentest_add_check_contains(NAME integration_list_tags_owner PROG $<TARGET_FILE:gentest_integration_tests> REQUIRED_SUBSTRING "owner=team-runtime" ARGS --list)
gentest_add_check_contains(
    NAME integration_list_tags_requires_shape
    PROG $<TARGET_FILE:gentest_integration_tests>
    REQUIRED_SUBSTRING "errors/recover [gentest:tags=owner=team-runtime;requires=BUG-123]"
    ARGS --list)
gentest_add_check_contains(
    NAME integration_list_skip_shape
    PROG $<TARGET_FILE:gentest_integration_tests>
    REQUIRED_SUBSTRING "errors/throw [gentest:tags=windows;skip=unstable]"
    ARGS --list)

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

# JUnit XML smoke test: generate and validate basic content
gentest_add_check_file_contains(
    NAME junit_smoke_unit
    PROG $<TARGET_FILE:gentest_unit_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml
    REQUIRED_SUBSTRING <testsuite
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit.xml)

gentest_add_check_file_contains(
    NAME junit_properties_unit
    PROG $<TARGET_FILE:gentest_unit_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml
    REQUIRED_SUBSTRING "<property name=\"requirement\" value=\"#42\""
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_unit_props.xml)

gentest_add_check_counts(NAME repeat_unit_twice PROG $<TARGET_FILE:gentest_unit_tests> PASS 40 FAIL 0 SKIP 0 ARGS --repeat=2)

gentest_add_check_counts(NAME failing_fail_fast PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 1 SKIP 0 ARGS --fail-fast)

gentest_add_check_contains(
    NAME shuffle_seed_print
    PROG $<TARGET_FILE:gentest_fixtures_tests>
    REQUIRED_SUBSTRING "Shuffle seed: 42"
    ARGS
        --run=fixtures/free/basic
        --shuffle
        --seed
        42)

gentest_add_check_exit_code(
    NAME fixtures_raw_pointer_polymorphic_exact_repeat
    PROG $<TARGET_FILE:gentest_fixtures_tests>
    EXPECT_RC 0
    ARGS --run=fixtures/free/raw_pointer_polymorphic_b_after_first --kind=test --repeat=2)

gentest_add_check_counts(
    NAME regression_fixture_group_shuffle_invariants
    PROG $<TARGET_FILE:gentest_regression_fixture_group_shuffle_invariants>
    PASS 10
    FAIL 0
    SKIP 0
    ARGS
        --kind=test
        --shuffle
        --seed
        1337)

option(GENTEST_ENABLE_ALLURE_TESTS "Enable Allure writer tests (off by default)" OFF)
if(GENTEST_ENABLE_ALLURE_TESTS)
    # Allure results smoke: single test writes a result file with passed status
    gentest_add_check_file_contains(
        NAME allure_smoke_single
        PROG $<TARGET_FILE:gentest_unit_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single/result-0-result.json
        REQUIRED_SUBSTRING "\"status\":\"passed\""
        ARGS
            --run=unit/arithmetic/sum
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single)
    set_tests_properties(allure_smoke_single PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME allure_blocked_shared_fixture_label
        PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureBlockedOutcome.cmake"
        ARGS
            --run=regressions/shared_setup_skip/suite_free
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_blocked_shared_fixture"
            "EXPECT_RC=1"
            "EXPECTED_REASON=shared fixture unavailable for 'regressions::shared_setup_skip::NullSuiteFx': fixture allocation returned null")
    set_tests_properties(allure_blocked_shared_fixture_label PROPERTIES LABELS "allure")

    # Allure logs attachment: failing test with LogPolicy::OnFailure should create a logs attachment
    gentest_add_run_and_check_file(
        NAME allure_logs_attachment
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_json/result-0-result.json
        REQUIRED_SUBSTRING "result-0-attachment.txt"
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_json)
    set_tests_properties(allure_logs_attachment PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_has_plain_text_type
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_typed/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"logs\",\"source\":\"result-0-attachment.txt\",\"type\":\"text/plain\""
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_typed)
    set_tests_properties(allure_logs_attachment_has_plain_text_type PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_file
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_file/result-0-attachment.txt
        REQUIRED_SUBSTRING "hello from log"
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_file)
    set_tests_properties(allure_logs_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_bench_metrics_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_metrics_json/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"metrics\",\"source\":\"result-0-metrics.tsv\",\"type\":\"text/tab-separated-values\""
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sqrt
            --kind=bench
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_metrics_json)
    set_tests_properties(allure_bench_metrics_attachment_json PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_bench_metrics_attachment_file
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_metrics_file/result-0-metrics.tsv
        REQUIRED_SUBSTRING "metric\tvalue\nname\tbenchmarks/math/sqrt"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sqrt
            --kind=bench
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_metrics_file)
    set_tests_properties(allure_bench_metrics_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_bench_plot_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_plot_json/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"summary-plot\",\"source\":\"result-0-summary-plot.svg\",\"type\":\"image/svg+xml\""
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sqrt
            --kind=bench
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_plot_json)
    set_tests_properties(allure_bench_plot_attachment_json PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_bench_plot_attachment_file
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_plot_file/result-0-summary-plot.svg
        REQUIRED_SUBSTRING "<svg"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sqrt
            --kind=bench
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_plot_file)
    set_tests_properties(allure_bench_plot_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_histogram_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_histogram_json/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"histogram\",\"source\":\"result-0-histogram.tsv\",\"type\":\"text/tab-separated-values\""
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_histogram_json)
    set_tests_properties(allure_jitter_histogram_attachment_json PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_histogram_attachment_file
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_histogram_file/result-0-histogram.tsv
        REQUIRED_SUBSTRING "bin\trange_lo_ns\trange_hi_ns\tinclusive_hi\tcount\tpercent\tcumulative_percent\n1\t"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_histogram_file)
    set_tests_properties(allure_jitter_histogram_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_plot_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_plot_json/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"histogram-plot\",\"source\":\"result-0-histogram-plot.svg\",\"type\":\"image/svg+xml\""
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_plot_json)
    set_tests_properties(allure_jitter_plot_attachment_json PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_plot_attachment_file
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_plot_file/result-0-histogram-plot.svg
        REQUIRED_SUBSTRING "<svg"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_plot_file)
    set_tests_properties(allure_jitter_plot_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_samples_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_json/result-0-result.json
        REQUIRED_SUBSTRING "\"name\":\"samples\",\"source\":\"result-0-samples.json\",\"type\":\"application/json\""
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_json)
    set_tests_properties(allure_jitter_samples_attachment_json PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_samples_attachment_file
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_file/result-0-samples.json
        REQUIRED_SUBSTRING "\"sample_count\":"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_file)
    set_tests_properties(allure_jitter_samples_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_jitter_samples_attachment_schema
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_schema/result-0-samples.json
        REQUIRED_SUBSTRING "\"samples_ns\":"
        EXPECT_RC 0
        ARGS
            --run=benchmarks/math/sin_jitter
            --kind=jitter
            --bench-min-epoch-time-s=0.0001
            --bench-epochs=1
            --bench-warmup=0
            --bench-max-total-time-s=0.005
            --jitter-bins=4
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_samples_schema)
    set_tests_properties(allure_jitter_samples_attachment_schema PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_allure_directory_failure_reports_infra
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureDirFailure.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/allure_dir_failure/output.json"
            "REQUIRED_SUBSTRING=failed to prepare Allure report directory:")
    set_tests_properties(regression_allure_directory_failure_reports_infra PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_allure_result_write_failure_reports_infra
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureResultWriteFailure.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_result_write_failure"
            "REQUIRED_SUBSTRING=failed to open Allure result:")
    set_tests_properties(regression_allure_result_write_failure_reports_infra PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_allure_result_write_failure_keeps_junit_in_sync
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureJunitParity.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_junit_parity"
            "JUNIT_PATH=${CMAKE_CURRENT_BINARY_DIR}/allure_junit_parity/result.xml"
            "REQUIRED_SUBSTRING=failed to open Allure result:"
            "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
            "EXPECT_JUNIT_COUNTS=tests=\"1\" failures=\"0\" skipped=\"0\" errors=\"1\"")
    set_tests_properties(regression_allure_result_write_failure_keeps_junit_in_sync PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_junit_open_failure_keeps_allure_in_sync
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckJunitAllureParity.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_parity"
            "JUNIT_PATH=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_parity/junit-as-directory"
            "REQUIRED_SUBSTRING=failed to open JUnit report:"
            "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
            "EXPECT_RESULT_COUNT=2")
    set_tests_properties(regression_junit_open_failure_keeps_allure_in_sync PROPERTIES LABELS "allure")

    if(UNIX AND EXISTS "/dev/full")
        gentest_add_cmake_script_test(
            NAME regression_junit_write_failure_keeps_allure_in_sync
            PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
            SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckJunitAllureParity.cmake"
            ARGS
                --run=regressions/runtime_reporting/pass_for_junit_io_visibility
                --kind=test
            DEFINES
                "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_write_parity"
                "JUNIT_PATH=/dev/full"
                "PREPARE_JUNIT_AS_DIRECTORY=OFF"
                "REQUIRED_SUBSTRING=failed to write JUnit report:"
                "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
                "EXPECT_RESULT_COUNT=2")
        set_tests_properties(regression_junit_write_failure_keeps_allure_in_sync PROPERTIES LABELS "allure")
    endif()

    gentest_add_cmake_script_test(
        NAME regression_allure_runner_infra_failure_parity
        PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
        SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureRunnerInfraParity.cmake"
        ARGS
            --run=regressions/shared_fixture_duplicate_registration/smoke
            --kind=test
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_runner_infra
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_runner_infra"
            "EXPECT_RC=1"
            "PASS_CASE_NAME=regressions/shared_fixture_duplicate_registration/smoke"
            "INFRA_SUBSTRING=registered multiple times with conflicting callbacks")
    set_tests_properties(regression_allure_runner_infra_failure_parity PROPERTIES LABELS "allure")

    gentest_add_check_exit_code(
        NAME regression_allure_attachment_name_collision
        PROG $<TARGET_FILE:gentest_regression_reporting_attachment_collision>
        EXPECT_RC 0
        ARGS ${CMAKE_CURRENT_BINARY_DIR}/allure_attachment_name_collision)
    set_tests_properties(regression_allure_attachment_name_collision PROPERTIES LABELS "allure")
endif()

gentest_add_cmake_script_test(
    NAME helper_check_file_contains_rejects_stale_output
    PROG ${CMAKE_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFileContainsRejectsStale.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake_stale_file_contains")

gentest_add_cmake_script_test(
    NAME helper_allure_runner_infra_parity_rejects_stale_output
    PROG ${CMAKE_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckAllureRunnerInfraParityRejectsStale.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake_stale_allure")

gentest_add_cmake_script_test(
    NAME gentest_cdash_experimental_dry_run
    PROG ${CMAKE_CTEST_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCdashExperimentalDryRun.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cmake_cdash_dry_run")

gentest_add_cmake_script_test(
    NAME gentest_cdash_experimental_live_smoke
    PROG ${CMAKE_CTEST_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCdashExperimentalLive.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cmake_cdash_live")

# Capture JUnit artifact example: unit suite emits JUnit; this smoke ensures the file is generated in build tree
gentest_add_check_file_contains(
    NAME junit_artifact_example
    PROG $<TARGET_FILE:gentest_integration_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml
    REQUIRED_SUBSTRING <testsuite
    ARGS --junit=${CMAKE_CURRENT_BINARY_DIR}/junit_integration.xml)

# Constructor exception handling across fixture lifetimes
gentest_add_check_death(
    NAME fixture_ctor_ephemeral_member
    PROG ${PROJECT_NAME}_ctor_tests
    REQUIRED_SUBSTRING "unexpected std::exception: ephemeral-ctor"
    ARGS --run=ctor/ephemeral_member)

gentest_add_check_death(
    NAME fixture_ctor_free_fixtures
    PROG ${PROJECT_NAME}_ctor_tests
    REQUIRED_SUBSTRING "unexpected std::exception: free-fx-ctor"
    ARGS --run=ctor/free_fixtures)

gentest_add_check_death(
    NAME fixture_ctor_suite_shared
    PROG ${PROJECT_NAME}_ctor_tests
    REQUIRED_SUBSTRING "fixture construction threw std::exception: suite-ctor"
    ARGS --run=ctor/suite)

gentest_add_check_death(
    NAME fixture_ctor_global_shared
    PROG ${PROJECT_NAME}_ctor_tests
    REQUIRED_SUBSTRING "fixture construction threw std::exception: global-ctor"
    ARGS --run=ctor/global)
# The 'ctor' suite intentionally contains failing tests; mark its raw run as WILL_FAIL
set_tests_properties(ctor PROPERTIES WILL_FAIL TRUE)
# Benchmarks: smoke checks for listing and suite table
gentest_add_check_contains(
    NAME benches_list_benches
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING benchmarks/string/concat_small
    ARGS --list-benches)

gentest_add_check_contains(
    NAME benches_table_smoke
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING benchmarks/fixture/free_suite_global
    ARGS
        --kind=bench
        --bench-table
        --run=benchmarks/fixture/free_suite_global
        --bench-epochs=1
        --bench-warmup=0
        --bench-min-epoch-time-s=0
        --bench-min-total-time-s=0)

gentest_add_check_contains(
    NAME benches_filter_space
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING "benchmarks/spacing/lock_guard_small<"
    ARGS --filter benchmarks/spacing/* --kind=bench)

gentest_add_check_death(
    NAME benches_filter_no_match
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING "benchmark filter matched 0 benchmarks"
    ARGS --filter=benchmarks/spacing/nope* --kind=bench)

gentest_add_check_death(
    NAME jitter_filter_no_match
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING "jitter filter matched 0 jitter benchmarks"
    ARGS --filter=benchmarks/spacing/nope* --kind=jitter)

gentest_add_check_death(
    NAME benches_filter_missing_arg
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING "error: --filter requires a value"
    ARGS --filter)

gentest_add_check_death(
    NAME benches_fixture_alloc_null
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING "shared fixture unavailable for 'benchmarks::NullBenchFixture'"
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
    REQUIRED_SUBSTRING "shared fixture unavailable for 'benchmarks::NullJitterFixture'"
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
    REQUIRED_SUBSTRING benchmarks/math/sin_jitter
    ARGS --list)

gentest_add_check_contains(
    NAME jitter_suite_table_smoke
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    REQUIRED_SUBSTRING benchmarks/math/sin_jitter
    ARGS --list)

gentest_add_run_and_check_file(
    NAME regression_bench_only_junit_generation
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_only_junit.xml
    REQUIRED_SUBSTRING "tests=\"1\""
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_only_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_only_junit_generation
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_only_junit.xml
    REQUIRED_SUBSTRING "tests=\"1\""
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global_jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_only_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_failure_junit_generation
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_failure_junit.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/bench_assert_should_fail --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_failure_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_failure_junit_generation
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_failure_junit.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/jitter_assert_should_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_failure_junit.xml)

gentest_add_check_death(
    NAME regression_bench_unknown_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "benchmark call failed for regressions/bench_unknown_exception_should_fail: unknown exception"
    ARGS --run=regressions/bench_unknown_exception_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_jitter_unknown_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "jitter call failed for regressions/jitter_unknown_exception_should_fail: unknown exception"
    ARGS --run=regressions/jitter_unknown_exception_should_fail --kind=jitter)

gentest_add_cmake_script_test(
    NAME regression_bench_warmup_assert_stops_before_measurement
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/bench_warmup_assert_should_stop_before_measurement
        --kind=bench
        --bench-warmup=1
        --bench-epochs=1
        --bench-min-total-time-s=0
        --bench-min-epoch-time-s=0
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=warmup benchmark assertion failure"
        "FORBID_SUBSTRING=regression marker: benchmark continued after warmup failure")

gentest_add_cmake_script_test(
    NAME regression_jitter_warmup_assert_stops_before_measurement
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/jitter_warmup_assert_should_stop_before_measurement
        --kind=jitter
        --bench-warmup=1
        --bench-epochs=1
        --bench-min-total-time-s=0
        --bench-min-epoch-time-s=0
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=warmup jitter assertion failure"
        "FORBID_SUBSTRING=regression marker: jitter continued after warmup failure")

gentest_add_cmake_script_test(
    NAME regression_outcome_consistency_matrix
    PROG $<TARGET_FILE:gentest_unit_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckOutcomeConsistencyMatrix.cmake"
    DEFINES
        "PROG_UNIT=$<TARGET_FILE:gentest_unit_tests>"
        "PROG_OUTCOMES=$<TARGET_FILE:gentest_outcomes_tests>"
        "PROG_INFRA_TEST=$<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>"
        "PROG_INFRA_MEASURED=$<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>"
        "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/outcome_consistency_matrix")

gentest_add_check_death(
    NAME cli_unknown_option
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "error: unknown option"
    ARGS --this-option-does-not-exist)

gentest_add_check_death(
    NAME cli_unknown_option_hard_fail
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "error: unknown option '--this-option-does-not-exist'"
    ARGS --run=unit/arithmetic/sum --this-option-does-not-exist)

gentest_add_check_death(
    NAME cli_removed_run_test
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "error: --run-test was removed; use --run"
    ARGS --run-test=unit/arithmetic/sum)

gentest_add_check_contains(
    NAME cli_kind_filter_zero_bench
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "Executed 0 benchmark(s)."
    ARGS --kind=bench)

gentest_add_check_death(
    NAME regression_cli_kind_mismatch_diagnostics
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "Case 'unit/arithmetic/sum' does not match --kind=bench"
    ARGS --run=unit/arithmetic/sum --kind=bench)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_diagnostics
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    REQUIRED_SUBSTRING "Case name is ambiguous: common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_lists_alpha
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    REQUIRED_SUBSTRING "regressions/ambiguity/alpha/common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_lists_beta
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    REQUIRED_SUBSTRING "regressions/ambiguity/beta/common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME cli_bench_table_invalid_kind
    PROG $<TARGET_FILE:gentest_unit_tests>
    REQUIRED_SUBSTRING "error: --bench-table requires --kind=bench or --kind=all"
    ARGS --bench-table --kind=jitter)

function(_gentest_add_cmake_helper_test name source_dir script_path label)
    # Keep helper fixture roots compact so nested CMake/Ninja scratch paths stay under
    # Windows toolchain path limits even when the outer checkout path is long.
    string(MD5 _gentest_helper_build_hash "${name}|${source_dir}|${script_path}")
    string(SUBSTRING "${_gentest_helper_build_hash}" 0 12 _gentest_helper_build_suffix)
    set(_gentest_helper_build_root "${CMAKE_CURRENT_BINARY_DIR}/cmh_${_gentest_helper_build_suffix}")

    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${source_dir}
            -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBUILD_ROOT=${_gentest_helper_build_root}
            -DGENERATOR=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCXX_COMPILER_CLANG_SCAN_DEPS=${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}
            -DPROG=${_gentest_codegen_prog}
            -DLLVM_DIR=${LLVM_DIR}
            -DClang_DIR=${Clang_DIR}
            -DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            ${ARGN}
            -P ${script_path})

    set_tests_properties(${name} PROPERTIES LABELS "${label}")
    set_property(TEST ${name} APPEND PROPERTY SKIP_REGULAR_EXPRESSION "GENTEST_SKIP_TEST:")
endfunction()

_gentest_add_cmake_helper_test(
    gentest_codegen_output_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenOutputCollision.cmake
    cmake
    -DREQUIRED_SUBSTRING=OUTPUT manifest/single-TU mode was removed)

_gentest_add_cmake_helper_test(
    gentest_codegen_genex_sources_rejected
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_genex_sources
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenOutputCollision.cmake
    cmake
    -DREQUIRED_SUBSTRING=generator-expression SOURCES entries are not supported)
set_property(TEST gentest_codegen_genex_sources_rejected APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_output_genex_rejected
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_genex
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenOutputCollision.cmake
    cmake
    -DREQUIRED_SUBSTRING=OUTPUT manifest/single-TU mode was removed)
set_property(TEST gentest_codegen_output_genex_rejected APPEND PROPERTY LABELS "codegen")

set(_gentest_discover_tests_config "")
if(CMAKE_CONFIGURATION_TYPES)
    set(_gentest_discover_tests_config "Debug")
endif()

_gentest_add_cmake_helper_test(
    gentest_discover_tests_smoke
    ${PROJECT_SOURCE_DIR}/tests/cmake/discover_tests
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckDiscoverTests.cmake
    cmake
    -DBUILD_CONFIG=${_gentest_discover_tests_config})

unset(_gentest_discover_tests_config)

_gentest_add_cmake_helper_test(
    gentest_tu_wrapper_source_props
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_wrapper_source_props
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTuWrapperSourceProps.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_tu_header_case_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_header_case_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTuHeaderCaseCollision.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_tu_header_case_collision APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_tu_register_symbol_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_register_symbol_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTuRegisterSymbolCollision.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_tu_register_symbol_collision APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_resource_dir_from_compdb_compiler
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenResourceDirFromCompdbCompiler.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_resource_dir_from_compdb_compiler APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_incremental_dependencies
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_incremental_deps
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenIncrementalDependencies.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_incremental_dependencies APPEND PROPERTY LABELS "codegen")

set(_gentest_fixture_build_config "")
if(CMAKE_CONFIGURATION_TYPES)
    set(_gentest_fixture_build_config "Debug")
endif()
set(_gentest_expect_public_modules OFF)
if(GENTEST_PUBLIC_MODULES_TOOLCHAIN_SUPPORTED)
    set(_gentest_expect_public_modules ON)
endif()

_gentest_add_cmake_helper_test(
    gentest_codegen_template_template_scope_bindings_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/template_template_scope_bindings
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=template_template_scope_bindings
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_template_template_scope_bindings_builds APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_defaulted_template_ctor_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/mock_defaulted_template_ctor
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DTARGET_NAME=mock_defaulted_template_ctor
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_mock_defaulted_template_ctor_builds APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_module_cpp_source_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_cpp_source
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=cpp_source_module_tests
    -DEXPECT_PUBLIC_MODULES=${_gentest_expect_public_modules}
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_module_cpp_source_builds APPEND PROPERTY LABELS "codegen;modules")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_template_template_pack_direct_expect_runs
    ${PROJECT_SOURCE_DIR}/tests/cmake/mock_template_template_pack_direct_expect
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMockTemplateTemplatePackDirectExpect.cmake
    cmake
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_mock_template_template_pack_direct_expect_runs APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_unnamed_template_template_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/mock_unnamed_template_template
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=mock_unnamed_template_template
    -DBUILD_CONFIG=${_gentest_fixture_build_config}
    -DBUILD_TIMEOUT_SEC=180)
set_property(TEST gentest_codegen_mock_unnamed_template_template_builds APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_defaulted_template_ctor_macro_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/mock_defaulted_template_ctor_macro
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=mock_defaulted_template_ctor_macro
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_mock_defaulted_template_ctor_macro_builds APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_template_template_ctor_traits_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/mock_template_template_ctor_traits
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=mock_template_template_ctor_traits
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_mock_template_template_ctor_traits_builds APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_deleted_annotated_case_builds
    ${PROJECT_SOURCE_DIR}/tests/cmake/deleted_annotated_case
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTARGET_NAME=deleted_annotated_case
    -DBUILD_CONFIG=${_gentest_fixture_build_config})
set_property(TEST gentest_codegen_deleted_annotated_case_builds APPEND PROPERTY LABELS "codegen")

gentest_add_cmake_script_test(
    NAME gentest_codegen_member_operator_template_keyword_builds
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMemberOperatorTemplateKeywordBuild.cmake"
    DEFINES
        "PROG=${_gentest_codegen_prog}"
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/member_operator_template_keyword_check"
        "GENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/member_operator_template_keyword"
        "TARGET_NAME=member_operator_template_keyword"
        "GENERATOR=${CMAKE_GENERATOR}"
        "MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "BUILD_CONFIG=${_gentest_fixture_build_config}"
        "C_COMPILER=${CMAKE_C_COMPILER}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "LLVM_DIR=${LLVM_DIR}"
        "Clang_DIR=${Clang_DIR}")
set_property(TEST gentest_codegen_member_operator_template_keyword_builds APPEND PROPERTY LABELS "cmake;codegen")

gentest_add_cmake_script_test(
    NAME gentest_codegen_check_cmake_fixture_build_build_config
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake"
    DEFINES
        "PROG=${_gentest_codegen_prog}"
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/cmake_fixture_build_config_check"
        "GENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_fixture_build_config"
        "TARGET_NAME=cmake_fixture_build_config"
        "GENERATOR=Ninja Multi-Config"
        "BUILD_CONFIG=Release"
        "C_COMPILER=${CMAKE_C_COMPILER}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}")
set_property(TEST gentest_codegen_check_cmake_fixture_build_build_config APPEND PROPERTY LABELS "cmake;codegen")

gentest_add_cmake_script_test(
    NAME gentest_codegen_genex_compile_definitions
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCmakeFixtureBuild.cmake"
    DEFINES
        "PROG=${_gentest_codegen_prog}"
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/genex_compile_definitions_check"
        "GENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/genex_compile_definitions"
        "TARGET_NAME=genex_compile_definitions"
        "GENERATOR=${CMAKE_GENERATOR}"
        "MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "BUILD_CONFIG=${_gentest_fixture_build_config}"
        "C_COMPILER=${CMAKE_C_COMPILER}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "LLVM_DIR=${LLVM_DIR}"
        "Clang_DIR=${Clang_DIR}")
set_property(TEST gentest_codegen_genex_compile_definitions APPEND PROPERTY LABELS "cmake;codegen")

gentest_add_cmake_script_test(
    NAME gentest_codegen_template_option_removed
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenTemplateOptionRemoved.cmake"
    DEFINES
        "PROG=${_gentest_codegen_prog}"
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/template_option_removed_check"
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "CXX_COMPILER=${CMAKE_CXX_COMPILER}")
set_property(TEST gentest_codegen_template_option_removed APPEND PROPERTY LABELS "cmake;codegen")

unset(_gentest_fixture_build_config)

_gentest_add_cmake_helper_test(
    gentest_codegen_tu_depfile_aggregation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenTuDepfileAggregation.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_tu_depfile_aggregation APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_depfile_write_failure
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenTuDepfileAggregation.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DMODE=write_failure)
set_property(TEST gentest_codegen_depfile_write_failure APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_tu_depfile_escaped_paths
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenTuDepfileAggregation.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DMODE=escaped_paths)
set_property(TEST gentest_codegen_tu_depfile_escaped_paths APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_cli_validation_warnings
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenCliValidationWarnings.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_cli_validation_warnings APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_legacy_manifest_mode_removed
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckLegacyManifestModeRemoved.cmake
    cmake
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_legacy_manifest_mode_removed APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_mock_manifest_split
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMockManifestSplit.cmake
    cmake
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_mock_manifest_split APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_module_mock_registration_manifest
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMockRegistrationManifest.cmake
    cmake
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_module_mock_registration_manifest APPEND PROPERTY LABELS "codegen;modules")

_gentest_add_cmake_helper_test(
    gentest_codegen_artifact_protocol_direct_invocation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckArtifactProtocolDirectInvocation.cmake
    cmake
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_artifact_protocol_direct_invocation APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_response_file_expansion
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenResponseFileExpansion.cmake
    cmake
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_response_file_expansion APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_synthetic_compdb_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSyntheticFallback.cmake
    cmake
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_codegen_synthetic_compdb_fallback APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_axis_generators
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DSMOKE_SOURCE=tests/smoke/codegen_axis_generators.cpp
    -DEXPECT_CASE_COUNT=45
    "-DREQUIRED_SUBSTRINGS=return range_many(1);;return range_descending_int(5);;return range_descending_int(3);;return range_descending_int(1);;return range_step_zero_int(1);;return range_step_zero_int(2);;return range_step_zero_int(3);;return range_float_many(0);;return range_float_many(0.25);;return range_float_many(0.5);;return range_float_descending(1);;return range_float_descending(0.75);;return range_float_descending(0.5);;return linspace_many(0.25);;return linspace_many_int(1);;return linspace_many_int(3);;return linspace_many_int(5);;return geom_many(8);;return geom_many_float(1.5);;return geom_many_float(3);;return geom_many_float(6);;return logspace_many(1000);;return logspace_many_int(1);;return logspace_many_int(2);;return logspace_many_int(4);;return logspace_many_int(8);;.name = \"smoke/axis_generators/logspace_many(0.10000000000000001)\"")
set_property(TEST gentest_codegen_emit_axis_generators APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_parameters_pack
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DSMOKE_SOURCE=tests/smoke/codegen_parameters_pack.cpp
    -DEXPECT_CASE_COUNT=6
    "-DREQUIRED_SUBSTRINGS=return pack_rows(1, 2);;return pack_rows(3, 4);;return pack_mixed(10, 20, 7);;return pack_mixed(30, 40, 8);;.name = \"smoke/parameters_pack/mixed(10, 20, 7)\"")
set_property(TEST gentest_codegen_emit_parameters_pack APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_fixture_resolution
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DSMOKE_SOURCE=tests/smoke/codegen_fixture_resolution.cpp
    -DEXPECT_CASE_COUNT=2
    "-DREQUIRED_SUBSTRINGS=#include \"gentest/detail/generated_runtime.h\";;gentest_init_fixture(fx0_, \"smoke::fixture_resolution::LocalFixture\");;gentest_init_shared_fixture(fx0_, ::gentest::detail::SharedFixtureScope::Suite, \"smoke/fixture_resolution\", \"smoke::fixture_resolution::SharedSuiteFixture\")"
    "-DREJECTED_SUBSTRINGS=#include \"gentest/detail/fixture_runtime.h\";;#include \"gentest/detail/registry_runtime.h\"")
set_property(TEST gentest_codegen_emit_fixture_resolution APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_template_fixture_resolution
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DEXPECT_COMPILE=ON
    -DSMOKE_SOURCE=tests/smoke/codegen_template_fixture_resolution.cpp
    -DEXPECT_CASE_COUNT=2)
set_property(TEST gentest_codegen_emit_template_fixture_resolution APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_template_parser_edges
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DSMOKE_SOURCE=tests/smoke/codegen_template_parser_edges.cpp
    -DEXPECT_CASE_COUNT=7
    "-DREQUIRED_SUBSTRINGS=return template_parser_template_template<std::list, std::pair<int, std::array<long, 2>>>();;return template_parser_template_template<std::vector, std::pair<int, std::array<long, 2>>>();;return template_parser_type_char<std::pair<int, std::array<long, 2>>, '\\''>();;return template_parser_type_char<std::pair<int, std::array<long, 2>>, '\\n'>();;return template_parser_syntax_sensitive<decltype(\"a,b\"), decltype(\"\\\"edge\\\"\"), std::integral_constant<int, 1> const>();;return template_parser_char_prefixes<u8'c', u'd', U'e', L'f'>();;.name = \"smoke/template_parser_edges/type_char<std::pair<int, std::array<long, 2>>,'\\\\''>\"")
set_property(TEST gentest_codegen_emit_template_parser_edges APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_emit_type_quoting
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenSmokeEmission.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DCOMPDB_ROOT=${CMAKE_BINARY_DIR}
    -DCODEGEN_STD=${_gentest_codegen_scan_std_flag}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg}
    -DSMOKE_SOURCE=tests/smoke/codegen_type_quoting.cpp
    -DEXPECT_CASE_COUNT=21
    "-DREQUIRED_SUBSTRINGS=return char_params('\\'');;return char_params('\\\\');;return char16_params(u'D');;return char32_params(U'F');;return char8_params(u8'C');;return utf16_cstr(u\"hello\");;return utf32_cstr(U\"Dog\");;return utf8_string_view(u8\"beta\");;return wchar_params(L'Q');;return wide_string_view(L\"Wide\")")
set_property(TEST gentest_codegen_emit_type_quoting APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_env_wrapped_compiler
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenEnvWrappedCompiler.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_env_wrapped_compiler APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_linux_thread_stack_size
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenLinuxThreadStackSize.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_linux_thread_stack_size APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_module_command_retargeting
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenModuleCommandRetargeting.cmake
    cmake)
set_property(TEST gentest_codegen_module_command_retargeting APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_forced_nonclang_compiler_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenForcedNonclangCompilerFallback.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_forced_nonclang_compiler_fallback APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_explicit_host_clang
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenExplicitHostClang.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_explicit_host_clang APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_module_cache_resource_dir_isolation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenModuleCacheResourceDirIsolation.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_module_cache_resource_dir_isolation APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_public_mock_module_gcc
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicMockModuleGccFailure.cmake
    cmake)
set_property(TEST gentest_public_mock_module_gcc APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_mixed_module_mock_registry
    ${PROJECT_SOURCE_DIR}/tests/cmake/mixed_module_mock_registry
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMixedModuleMockRegistry.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_mixed_module_mock_registry APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_mock_additive_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_additive_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMockAdditiveVisibility.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_additive_visibility APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_mock_imported_sibling
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_imported_sibling
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMockImportedSibling.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_imported_sibling APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_mock_multi_imported_sibling
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_multi_imported_sibling
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMockMultiImportedSibling.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_multi_imported_sibling APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_explicit_mock_target_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetSurface.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_surface APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_explicit_mock_target_validation
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_validation
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetValidation.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_validation APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_explicit_mock_target_staging
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetStaging.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_staging APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_explicit_mock_target_install_export
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetInstallExport.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_install_export APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_pcm_cache_isolation
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_pcm_cache_isolation
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModulePcmCacheIsolation.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_pcm_cache_isolation APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_auto_discovery
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_autodiscovery
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleAutoDiscovery.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_auto_discovery APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_registration_manifest
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_registration_manifest
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleRegistrationManifest.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_registration_manifest APPEND PROPERTY LABELS "modules;codegen")

_gentest_add_cmake_helper_test(
    gentest_module_registration_mock_split
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_registration_mock_split
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleRegistrationMockSplit.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_registration_mock_split APPEND PROPERTY LABELS "modules;codegen")

_gentest_add_cmake_helper_test(
    gentest_textual_wrapper_artifact_manifest
    ${PROJECT_SOURCE_DIR}/tests/cmake/textual_wrapper_artifact_manifest
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTextualWrapperArtifactManifest.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_textual_wrapper_artifact_manifest APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_module_multiline_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_multiline_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMultilineDirectiveFailure.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_multiline_directives APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_same_line_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_same_line_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleSameLineDirectives.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_same_line_directives APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_cpp_source
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_cpp_source
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleCppSource.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_cpp_source APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_cpp_source_in_tree_codegen
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_cpp_source
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleCppSource.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DNO_EXPLICIT_CODEGEN=ON)
set_property(TEST gentest_module_cpp_source_in_tree_codegen APPEND PROPERTY LABELS "modules;codegen")

_gentest_add_cmake_helper_test(
    gentest_module_first_classic_order
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_first_classic_order
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleFirstClassicOrder.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_first_classic_order APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_manual_include_whitespace
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_manual_include_whitespace
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleManualIncludeWhitespaceDuplicate.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_manual_include_whitespace APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_partial_manual_codegen_includes
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_partial_manual_codegen_includes
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModulePartialManualCodegenIncludes.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DTSAN_BUILD=${_gentest_is_tsan_build})
set_property(TEST gentest_module_partial_manual_codegen_includes APPEND PROPERTY LABELS "modules")

if(GENTEST_ENABLE_TSAN_STRESS_TESTS)
    _gentest_add_cmake_helper_test(
        gentest_module_partial_manual_codegen_tsan_race_repro
        ${PROJECT_SOURCE_DIR}/tests/cmake/module_partial_manual_codegen_includes
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModulePartialManualCodegenTsanRaceRepro.cmake
        cmake
        -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTSAN_BUILD=${_gentest_is_tsan_build}
        -DREPRO_TIMEOUT_SEC=${GENTEST_TSAN_STRESS_TIMEOUT_SEC})
    set_tests_properties(gentest_module_partial_manual_codegen_tsan_race_repro PROPERTIES LABELS "cmake;modules;stress;tsan")
    math(EXPR _gentest_tsan_race_repro_test_timeout "${GENTEST_TSAN_STRESS_TIMEOUT_SEC} + 120")
    set_tests_properties(gentest_module_partial_manual_codegen_tsan_race_repro PROPERTIES TIMEOUT "${_gentest_tsan_race_repro_test_timeout}")
    unset(_gentest_tsan_race_repro_test_timeout)
endif()

_gentest_add_cmake_helper_test(
    gentest_module_manual_partial_includes
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_manual_partial_includes
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleManualPartialIncludes.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_manual_partial_includes APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_name_literal_false_match
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleNameLiteralFalseMatch.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_name_literal_false_match APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_header_unit_import_preamble
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_header_unit_import_preamble
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleHeaderUnitImportPreamble.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_header_unit_import_preamble APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_partition_import_shorthand
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_partition_import_shorthand
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModulePartitionImportShorthand.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_partition_import_shorthand APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_old_ninja_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/public_module_support_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleSupportFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DFALLBACK_MODE=old_ninja)
set_property(TEST gentest_public_module_old_ninja_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_missing_scan_deps_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/public_module_support_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleSupportFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DFALLBACK_MODE=missing_scan_deps)
set_property(TEST gentest_public_module_missing_scan_deps_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_clang_scan_deps_program_resolution
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckClangScanDepsProgramResolution.cmake
    cmake)
set_property(TEST gentest_clang_scan_deps_program_resolution APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_fixture_compiler_resolution
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleFixtureCompilerResolution.cmake
    cmake)
set_property(TEST gentest_module_fixture_compiler_resolution APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_gcc_version_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleGccVersionFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_public_module_gcc_version_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_gcc_tsan_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleGccTsanFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_public_module_gcc_tsan_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/public_module_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleSurface.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_public_module_surface APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_codegen_public_module_imports
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenPublicModuleImports.cmake
    modules
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_public_module_imports APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_compile_command_macro_scan
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenCompileCommandMacroScan.cmake
    modules
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_compile_command_macro_scan APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_wrapped_main_discovery
    ${PROJECT_SOURCE_DIR}/tests/cmake/wrapped_main_discovery
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckWrappedMainDiscovery.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_wrapped_main_discovery APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_manifest_header_shared_fixture_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/manifest_header_shared_fixture_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckManifestHeaderSharedFixtureVisibility.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_manifest_header_shared_fixture_visibility APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_external_include_module_resolution
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCodegenExternalIncludeModuleResolution.cmake
    modules
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_external_include_module_resolution APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_public_module_detail_hidden
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPublicModuleDetailHidden.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_public_module_detail_hidden APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_outcome_matrix_stale_junit_guard
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckOutcomeConsistencyMatrixStaleJunit.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_windows_debug_death_skip_gate
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckWindowsDebugDeathSkipGate.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_tsan_death_skip_gate
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTsanDeathSkipGate.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_runtime_shared_context_exports
    ${PROJECT_SOURCE_DIR}/tests/cmake/runtime_shared_context_exports
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckRuntimeSharedContextExports.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_runtime_shared_context_exports APPEND PROPERTY LABELS "runtime")

_gentest_add_cmake_helper_test(
    gentest_install_only_codegen_default
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckInstallOnlyCodegenDefault.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})

_gentest_add_cmake_helper_test(
    gentest_subproject_consumer_defaults
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckSubprojectConsumerDefaults.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})

_gentest_add_cmake_helper_test(
    gentest_plain_tu_wrapper_no_module_scan
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPlainTuWrapperNoModuleScan.cmake
    codegen
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})

_gentest_add_cmake_helper_test(
    gentest_fmt_version_metadata_guard
    ${PROJECT_SOURCE_DIR}/tests/cmake/fmt_version_metadata_guard
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckFmtVersionMetadataGuard.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})

_gentest_add_cmake_helper_test(
    gentest_module_mock_long_domain_outputs
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_long_domain_outputs
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleMockLongDomainOutputs.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_long_domain_outputs APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_scan_include_clang_dir_append
    ${PROJECT_SOURCE_DIR}/tests/cmake/scan_include_clang_dir_append
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckScanIncludeClangDirAppend.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_scan_include_clang_dir_append APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_module_partition_registration_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_partition_registration_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModulePartitionRegistrationVisibility.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_partition_registration_visibility APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_registration_without_import
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_registration_without_import
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleRegistrationWithoutImport.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_registration_without_import APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_cross_compile_emulator_serialization
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCrossCompileEmulatorSerialization.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_cross_compile_exit_code_no_emulator
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCrossCompileExitCodeNoEmulator.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_missing_host_codegen_path
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMissingHostCodegenPath.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_coverage_workflow_serial_ctest
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCoverageWorkflowSerialCtest.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_coverage_report_script_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCoverageReportScriptContract.cmake
    contract)
set_property(TEST gentest_coverage_report_script_contract APPEND PROPERTY LABELS "contract" "docs")

_gentest_add_cmake_helper_test(
    gentest_coverage_report_script_smoke
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCoverageReportScriptSmoke.cmake
    contract)
set_property(TEST gentest_coverage_report_script_smoke APPEND PROPERTY LABELS "contract")

_gentest_add_cmake_helper_test(
    gentest_coverage_hygiene_script_smoke
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckCoverageHygieneScriptSmoke.cmake
    contract)
set_property(TEST gentest_coverage_hygiene_script_smoke APPEND PROPERTY LABELS "contract")

_gentest_add_cmake_helper_test(
    gentest_diagnostic_workflow_lanes
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckDiagnosticWorkflowLanes.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_clang_tidy_script_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckClangTidyScriptContract.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_clang_tidy_arguments_materialization
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckClangTidyArgumentsMaterialization.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_tidy_context_adoption
    ${PROJECT_SOURCE_DIR}/tests/cmake/tidy_context_adoption
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTidyContextAdoption.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_workflow_action_versions
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckWorkflowActionVersions.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_buildsystems_linux_workflow_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBuildsystemsLinuxWorkflowContract.cmake
    cmake)
set_property(TEST gentest_buildsystems_linux_workflow_contract APPEND PROPERTY LABELS "contract")

_gentest_add_cmake_helper_test(
    gentest_native_buildsystem_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNativeBuildsystemContract.cmake
    cmake)
set_property(TEST gentest_native_buildsystem_contract APPEND PROPERTY LABELS "contract")

_gentest_add_cmake_helper_test(
    gentest_meson_textual_fmt_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMesonTextualFmtFallback.cmake
    cmake)
set_property(TEST gentest_meson_textual_fmt_fallback APPEND PROPERTY LABELS "meson")

_gentest_add_cmake_helper_test(
    gentest_meson_wrap_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckMesonWrapConsumer.cmake
    cmake)
set_property(TEST gentest_meson_wrap_consumer APPEND PROPERTY LABELS "meson")

_gentest_add_cmake_helper_test(
    gentest_xmake_textual_consumer_registration
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckXmakeTextualConsumerRegistration.cmake
    cmake)
set_property(TEST gentest_xmake_textual_consumer_registration APPEND PROPERTY LABELS "xmake")

_gentest_add_cmake_helper_test(
    gentest_xmake_textual_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckXmakeTextualConsumer.cmake
    cmake)
set_property(TEST gentest_xmake_textual_consumer APPEND PROPERTY LABELS "xmake")

_gentest_add_cmake_helper_test(
    gentest_xmake_module_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckXmakeModuleConsumer.cmake
    cmake)
set_property(TEST gentest_xmake_module_consumer APPEND PROPERTY LABELS "xmake" "modules")

_gentest_add_cmake_helper_test(
    gentest_xmake_xrepo_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckXmakeXrepoConsumer.cmake
    cmake)
set_property(TEST gentest_xmake_xrepo_consumer APPEND PROPERTY LABELS "xmake")

_gentest_add_cmake_helper_test(
    gentest_bazel_textual_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBazelTextualConsumer.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_bazel_module_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBazelModuleConsumer.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_bazel_bzlmod_consumer
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBazelBzlmodConsumer.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_bazel_helpers_use_configured_toolchain
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBazelHelperConfiguredToolchain.cmake
    contract)

_gentest_add_cmake_helper_test(
    gentest_bazel_module_embed_flag_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckBazelModuleEmbedFlagContract.cmake
    contract)
set_property(TEST gentest_bazel_module_embed_flag_contract APPEND PROPERTY LABELS "bazel" "modules" "contract")

_gentest_add_cmake_helper_test(
    gentest_module_doc_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckModuleDocContract.cmake
    contract)
set_property(TEST gentest_module_doc_contract APPEND PROPERTY LABELS "modules" "docs")

_gentest_add_cmake_helper_test(
    gentest_examples_doc_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExamplesContract.cmake
    contract)
set_property(TEST gentest_examples_doc_contract APPEND PROPERTY LABELS "docs" "examples" "modules")

_gentest_add_cmake_helper_test(
    gentest_syntax_sensitive_regression_fixtures
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckSyntaxSensitiveRegressionFixtures.cmake
    contract)
set_property(TEST gentest_syntax_sensitive_regression_fixtures APPEND PROPERTY LABELS "modules" "codegen")

_gentest_add_cmake_helper_test(
    gentest_non_cmake_module_contract
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNonCmakeModuleContract.cmake
    cmake)
set_property(TEST gentest_non_cmake_module_contract APPEND PROPERTY LABELS "contract")

_gentest_add_cmake_helper_test(
    gentest_qemu_workflow_presets
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckQemuWorkflowPresets.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_qemu_toolchain_runtime_root_derivation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckQemuToolchainRuntimeRootDerivation.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_package_workflow_preset
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageWorkflowPreset.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_vcpkg_manifest_metadata
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckVcpkgManifestMetadata.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_package_consumer_workdir_isolation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumerWorkdirIsolation.cmake
    package
    -DPACKAGE_NAME=${PROJECT_NAME})
set_property(TEST gentest_package_consumer_workdir_isolation APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_package_consumer_executable_path
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumerExecutablePath.cmake
    package
    -DPACKAGE_NAME=${PROJECT_NAME})
set_property(TEST gentest_package_consumer_executable_path APPEND PROPERTY LABELS "modules")

option(GENTEST_ENABLE_PACKAGE_TESTS "Enable install + find_package consumer smoke test (FetchContent)" OFF)
if(GENTEST_ENABLE_PACKAGE_TESTS)
    set(_gentest_package_test_config "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_gentest_package_test_config "Debug")
    endif()

    set(_gentest_package_test_compiler_args "")
    if(DEFINED CMAKE_C_COMPILER AND NOT "${CMAKE_C_COMPILER}" STREQUAL "")
        list(APPEND _gentest_package_test_compiler_args
            -DPACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
    if(DEFINED CMAKE_CXX_COMPILER AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
        list(APPEND _gentest_package_test_compiler_args
            -DPACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    if(DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
        list(APPEND _gentest_package_test_compiler_args
            -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS})
    endif()

    set(_gentest_package_test_module_compiler_args ${_gentest_package_test_compiler_args})
    if(UNIX)
        gentest_resolve_clang_fixture_compilers(_gentest_package_test_module_c _gentest_package_test_module_cxx)
        if(NOT "${_gentest_package_test_module_c}" STREQUAL "" AND NOT "${_gentest_package_test_module_cxx}" STREQUAL "")
            set(_gentest_package_test_module_compiler_args
                -DPACKAGE_TEST_C_COMPILER=${_gentest_package_test_module_c}
                -DPACKAGE_TEST_CXX_COMPILER=${_gentest_package_test_module_cxx}
                -DPACKAGE_TEST_EXPECT_MODULES=ON)
            gentest_find_clang_scan_deps(_gentest_package_test_module_scan_deps "${_gentest_package_test_module_cxx}")
            if(NOT "${_gentest_package_test_module_scan_deps}" STREQUAL "")
                list(APPEND _gentest_package_test_module_compiler_args
                    -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${_gentest_package_test_module_scan_deps})
            endif()
        endif()
    endif()

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_include_only
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_legacy_detail_contract
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumerLegacyDetailContract.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_include_only_native_codegen
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_runtime_only_include_only
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=runtime_only
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_runtime_only_include_only_native_codegen
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=runtime_only
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_double_include_only
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=double
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_double_include_only_native_codegen
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=double
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer APPEND PROPERTY LABELS "modules")

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_native_codegen
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_native_codegen APPEND PROPERTY LABELS "modules")

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_runtime_only
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=runtime_only
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_runtime_only APPEND PROPERTY LABELS "modules")

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_double
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=double
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_double APPEND PROPERTY LABELS "modules")

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_relwithdebinfo_exact_config
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        -DBUILD_TYPE=RelWithDebInfo
        -DBUILD_CONFIG=RelWithDebInfo
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_relwithdebinfo_exact_config APPEND PROPERTY LABELS "modules" "package")

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_minsizerel_exact_config
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        -DBUILD_TYPE=MinSizeRel
        -DBUILD_CONFIG=MinSizeRel
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_minsizerel_exact_config APPEND PROPERTY LABELS "modules" "package")

    if(UNIX AND NOT APPLE)
        find_program(_gentest_package_test_gcc NAMES gcc)
        find_program(_gentest_package_test_gxx NAMES g++)
        if(_gentest_package_test_gcc AND _gentest_package_test_gxx)
            _gentest_add_cmake_helper_test(
                gentest_package_consumer_gcc
                ${PROJECT_SOURCE_DIR}
                ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckPackageConsumer.cmake
                package
                -DPACKAGE_NAME=${PROJECT_NAME}
                -DBUILD_CONFIG=${_gentest_package_test_config}
                -DCONSUMER_LINK_MODE=main_only
                -DPACKAGE_TEST_USE_MODULES=ON
                -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
                -DPACKAGE_TEST_C_COMPILER=${_gentest_package_test_gcc}
                -DPACKAGE_TEST_CXX_COMPILER=${_gentest_package_test_gxx})
            set_property(TEST gentest_package_consumer_gcc APPEND PROPERTY LABELS "modules")
        endif()
        unset(_gentest_package_test_gcc)
        unset(_gentest_package_test_gxx)
    endif()

    unset(_gentest_package_test_config)
    unset(_gentest_package_test_compiler_args)
    unset(_gentest_package_test_module_compiler_args)
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
