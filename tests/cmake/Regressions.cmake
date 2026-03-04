# Extracted from tests/CMakeLists.txt to keep the top-level test list manageable.
# Regression binaries (manual runtime registration to isolate runner edge cases)
add_executable(gentest_regression_bench_assert
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/bench_assert_propagation.cpp)
target_link_libraries(gentest_regression_bench_assert PRIVATE gentest_runtime)
target_compile_features(gentest_regression_bench_assert PRIVATE cxx_std_20)
target_include_directories(gentest_regression_bench_assert PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_reentry
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_reentry.cpp)
target_link_libraries(gentest_regression_shared_fixture_reentry PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_reentry PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_reentry PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_teardown_exit
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_teardown_exit.cpp)
target_link_libraries(gentest_regression_shared_fixture_teardown_exit PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_teardown_exit PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_teardown_exit PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_member_shared_fixture_setup_skip
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/member_shared_fixture_setup_skip.cpp)
target_link_libraries(gentest_regression_member_shared_fixture_setup_skip PRIVATE gentest_runtime)
target_compile_features(gentest_regression_member_shared_fixture_setup_skip PRIVATE cxx_std_20)
target_include_directories(gentest_regression_member_shared_fixture_setup_skip PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_member_shared_fixture_setup_skip_bench_jitter
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/member_shared_fixture_setup_skip_bench_jitter.cpp)
target_link_libraries(gentest_regression_member_shared_fixture_setup_skip_bench_jitter PRIVATE gentest_runtime)
target_compile_features(gentest_regression_member_shared_fixture_setup_skip_bench_jitter PRIVATE cxx_std_20)
target_include_directories(gentest_regression_member_shared_fixture_setup_skip_bench_jitter PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_duplicate_registration
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_duplicate_registration.cpp)
target_link_libraries(gentest_regression_shared_fixture_duplicate_registration PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_duplicate_registration PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_duplicate_registration PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_duplicate_registration_idempotent
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_duplicate_registration_idempotent.cpp)
target_link_libraries(gentest_regression_shared_fixture_duplicate_registration_idempotent PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_duplicate_registration_idempotent PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_duplicate_registration_idempotent PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_scope_conflict
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_scope_conflict.cpp)
target_link_libraries(gentest_regression_shared_fixture_scope_conflict PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_scope_conflict PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_scope_conflict PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_ordering
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_ordering.cpp)
target_link_libraries(gentest_regression_shared_fixture_ordering PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_ordering PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_ordering PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_fixture_group_shuffle_invariants
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/fixture_group_shuffle_invariants.cpp)
target_link_libraries(gentest_regression_fixture_group_shuffle_invariants PRIVATE gentest_runtime)
target_compile_features(gentest_regression_fixture_group_shuffle_invariants PRIVATE cxx_std_20)
target_include_directories(gentest_regression_fixture_group_shuffle_invariants PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_manual_create_throw_skip
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_manual_create_throw_skip.cpp)
target_link_libraries(gentest_regression_shared_fixture_manual_create_throw_skip PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_manual_create_throw_skip PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_manual_create_throw_skip PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_manual_setup_throw_skip
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_manual_setup_throw_skip.cpp)
target_link_libraries(gentest_regression_shared_fixture_manual_setup_throw_skip PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_manual_setup_throw_skip PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_manual_setup_throw_skip PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_missing_factory_skip
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_missing_factory_skip.cpp)
target_link_libraries(gentest_regression_shared_fixture_missing_factory_skip PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_missing_factory_skip PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_missing_factory_skip PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_runtime_registration_during_run
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_runtime_registration_during_run.cpp)
target_link_libraries(gentest_regression_shared_fixture_runtime_registration_during_run PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_runtime_registration_during_run PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_runtime_registration_during_run PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_runtime_reentry_rejected
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_runtime_reentry_rejected.cpp)
target_link_libraries(gentest_regression_shared_fixture_runtime_reentry_rejected PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_runtime_reentry_rejected PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_runtime_reentry_rejected PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_suite_scope_descendant
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_suite_scope_descendant.cpp)
target_link_libraries(gentest_regression_shared_fixture_suite_scope_descendant PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_suite_scope_descendant PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_suite_scope_descendant PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_suite_scope_most_specific
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_suite_scope_most_specific.cpp)
target_link_libraries(gentest_regression_shared_fixture_suite_scope_most_specific PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_suite_scope_most_specific PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_suite_scope_most_specific PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_suite_scope_prefix_collision
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_suite_scope_prefix_collision.cpp)
target_link_libraries(gentest_regression_shared_fixture_suite_scope_prefix_collision PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_suite_scope_prefix_collision PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_suite_scope_prefix_collision PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_shared_fixture_manual_teardown_throw_exit
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_manual_teardown_throw_exit.cpp)
target_link_libraries(gentest_regression_shared_fixture_manual_teardown_throw_exit PRIVATE gentest_runtime)
target_compile_features(gentest_regression_shared_fixture_manual_teardown_throw_exit PRIVATE cxx_std_20)
target_include_directories(gentest_regression_shared_fixture_manual_teardown_throw_exit PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_cli_suffix_ambiguity
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/cli_suffix_ambiguity.cpp)
target_link_libraries(gentest_regression_cli_suffix_ambiguity PRIVATE gentest_runtime)
target_compile_features(gentest_regression_cli_suffix_ambiguity PRIVATE cxx_std_20)
target_include_directories(gentest_regression_cli_suffix_ambiguity PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_orchestrator_fail_fast_blocks_measured
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/orchestrator_fail_fast_blocks_measured.cpp)
target_link_libraries(gentest_regression_orchestrator_fail_fast_blocks_measured PRIVATE gentest_runtime)
target_compile_features(gentest_regression_orchestrator_fail_fast_blocks_measured PRIVATE cxx_std_20)
target_include_directories(gentest_regression_orchestrator_fail_fast_blocks_measured PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_measured_local_fixture_partial_setup_teardown
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_partial_setup_teardown.cpp)
target_link_libraries(gentest_regression_measured_local_fixture_partial_setup_teardown PRIVATE gentest_runtime)
target_compile_features(gentest_regression_measured_local_fixture_partial_setup_teardown PRIVATE cxx_std_20)
target_include_directories(gentest_regression_measured_local_fixture_partial_setup_teardown PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_measured_local_fixture_setup_throw_teardown_armed
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_throw_teardown_armed.cpp)
target_link_libraries(gentest_regression_measured_local_fixture_setup_throw_teardown_armed PRIVATE gentest_runtime)
target_compile_features(gentest_regression_measured_local_fixture_setup_throw_teardown_armed PRIVATE cxx_std_20)
target_include_directories(gentest_regression_measured_local_fixture_setup_throw_teardown_armed PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_measured_local_fixture_call_teardown_dualfail
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_call_teardown_dualfail.cpp)
target_link_libraries(gentest_regression_measured_local_fixture_call_teardown_dualfail PRIVATE gentest_runtime)
target_compile_features(gentest_regression_measured_local_fixture_call_teardown_dualfail PRIVATE cxx_std_20)
target_include_directories(gentest_regression_measured_local_fixture_call_teardown_dualfail PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_measured_local_fixture_setup_skip_teardown_fail
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_fail.cpp)
target_link_libraries(gentest_regression_measured_local_fixture_setup_skip_teardown_fail PRIVATE gentest_runtime)
target_compile_features(gentest_regression_measured_local_fixture_setup_skip_teardown_fail PRIVATE cxx_std_20)
target_include_directories(gentest_regression_measured_local_fixture_setup_skip_teardown_fail PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_measured_local_fixture_setup_skip_teardown_skip
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_skip.cpp)
target_link_libraries(gentest_regression_measured_local_fixture_setup_skip_teardown_skip PRIVATE gentest_runtime)
target_compile_features(gentest_regression_measured_local_fixture_setup_skip_teardown_skip PRIVATE cxx_std_20)
target_include_directories(gentest_regression_measured_local_fixture_setup_skip_teardown_skip PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_suite(regression_local_fixture_teardown
    TARGET gentest_regression_local_fixture_teardown
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/local_fixture_teardown_on_throw.cpp
    OUTPUT_DIR regressions/local_fixture_teardown
    NO_CTEST)

gentest_add_suite(regression_local_fixture_teardown_noexceptions
    TARGET gentest_regression_local_fixture_teardown_noexceptions
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/local_fixture_teardown_noexceptions.cpp
    OUTPUT_DIR regressions/local_fixture_teardown_noexceptions
    NO_CTEST)
target_compile_options(gentest_regression_local_fixture_teardown_noexceptions
    PRIVATE
        $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:/EHs-c->
        $<$<NOT:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>>:-fno-exceptions>)
target_compile_definitions(gentest_regression_local_fixture_teardown_noexceptions
    PRIVATE
        FMT_EXCEPTIONS=0
        _HAS_EXCEPTIONS=0)

gentest_add_suite(regression_shared_fixture_setup_skip
    TARGET gentest_regression_shared_fixture_setup_skip
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_setup_skip.cpp
    OUTPUT_DIR regressions/shared_fixture_setup_skip
    NO_CTEST)

gentest_add_suite(regression_shared_fixture_setup_skip_bench_jitter
    TARGET gentest_regression_shared_fixture_setup_skip_bench_jitter
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_setup_skip_bench_jitter.cpp
    OUTPUT_DIR regressions/shared_fixture_setup_skip_bench_jitter
    NO_CTEST)

gentest_add_suite(regression_measured_generated_local_fixture_partial_setup_teardown
    TARGET gentest_regression_mg_lf_partial_teardown
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_generated_local_fixture_partial_setup_teardown.cpp
    OUTPUT_DIR regressions/mg_lf_partial_teardown
    NO_CTEST)

gentest_add_suite(regression_measured_generated_local_fixture_setup_throw_teardown_armed
    TARGET gentest_regression_mg_lf_setup_throw_teardown
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_generated_local_fixture_setup_throw_teardown_armed.cpp
    OUTPUT_DIR regressions/mg_lf_setup_throw_teardown
    NO_CTEST)

gentest_add_suite(regression_measured_generated_local_fixture_partial_setup_teardown_noexceptions
    TARGET gentest_regression_mg_lf_partial_teardown_noexc
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_generated_local_fixture_partial_setup_teardown.cpp
    OUTPUT_DIR regressions/mg_lf_partial_teardown_noexc
    NO_CTEST)

gentest_add_suite(regression_measured_generated_local_fixture_setup_throw_teardown_armed_noexceptions
    TARGET gentest_regression_mg_lf_setup_throw_teardown_noexc
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_generated_local_fixture_setup_throw_teardown_armed.cpp
    OUTPUT_DIR regressions/mg_lf_setup_throw_teardown_noexc
    NO_CTEST)

foreach(_gentest_noexc_target
        gentest_regression_mg_lf_partial_teardown_noexc
        gentest_regression_mg_lf_setup_throw_teardown_noexc)
    target_compile_options(${_gentest_noexc_target}
        PRIVATE
            $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:/EHs-c->
            $<$<NOT:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>>:-fno-exceptions>)
    target_compile_definitions(${_gentest_noexc_target}
        PRIVATE
            FMT_EXCEPTIONS=0
            _HAS_EXCEPTIONS=0
            GENTEST_EXPECT_NO_EXCEPTIONS=1)
endforeach()

add_executable(gentest_regression_time_unit_scaling
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/time_unit_scaling.cpp)
target_link_libraries(gentest_regression_time_unit_scaling PRIVATE gentest_runtime)
target_compile_features(gentest_regression_time_unit_scaling PRIVATE cxx_std_20)
target_include_directories(gentest_regression_time_unit_scaling PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gentest_regression_runtime_reporting
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/runtime_reporting_regressions.cpp)
target_link_libraries(gentest_regression_runtime_reporting PRIVATE gentest_runtime)
target_compile_features(gentest_regression_runtime_reporting PRIVATE cxx_std_20)
target_include_directories(gentest_regression_runtime_reporting PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME regression_bench_assert_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "benchmark call failed for regressions/bench_assert_should_fail"
    ARGS --run=regressions/bench_assert_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_jitter_assert_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "jitter call failed for regressions/jitter_assert_should_fail"
    ARGS --run=regressions/jitter_assert_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_bench_std_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "benchmark call failed for regressions/bench_std_exception_should_fail"
    ARGS --run=regressions/bench_std_exception_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_fail_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "benchmark call failed for regressions/bench_fail_should_fail"
    ARGS --run=regressions/bench_fail_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_skip_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "benchmark call failed for regressions/bench_skip_should_fail"
    ARGS --run=regressions/bench_skip_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_jitter_std_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "jitter call failed for regressions/jitter_std_exception_should_fail"
    ARGS --run=regressions/jitter_std_exception_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_fail_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "jitter call failed for regressions/jitter_fail_should_fail"
    ARGS --run=regressions/jitter_fail_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_skip_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_SUBSTRING "jitter call failed for regressions/jitter_skip_should_fail"
    ARGS --run=regressions/jitter_skip_should_fail --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_runtime_reporting_fallback_assertion_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_fallback_assertion.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/runtime_reporting/fallback_assertion_failure --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_fallback_assertion.xml)

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_github_annotation_escapes_file_title
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/runtime_reporting/gha,title:punct --kind=test --github-annotations
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=::error file=C%3A/repo%2Cwin/src/runtime_reporting_case.cpp,line=77,title=regressions/runtime_reporting/gha%2Ctitle%3Apunct::"
        "FORBID_SUBSTRING=::error file=C:/repo,win/src/runtime_reporting_case.cpp,line=77,title=regressions/runtime_reporting/gha,title:punct::")

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_junit_cdata_token_split
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckRuntimeReportingCdata.cmake"
    ARGS --run=regressions/runtime_reporting/junit_cdata_close_token_failure --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_cdata_token.xml
    DEFINES
        "JUNIT_FILE=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_cdata_token.xml"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_junit_open_failure_is_visible
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/runtime_reporting/pass_for_junit_io_visibility --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=failed to open JUnit report:")

set(_gentest_measured_line_files
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/bench_assert_propagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_call_teardown_dualfail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_partial_setup_teardown.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_fail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_skip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_throw_teardown_armed.cpp")
string(JOIN "|" _gentest_measured_line_files_arg ${_gentest_measured_line_files})
add_test(NAME regression_runtime_reporting_measured_case_lines_use___line__
    COMMAND ${CMAKE_COMMAND}
        -DFILES=${_gentest_measured_line_files_arg}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckNoLiteralCaseLines.cmake)
set_property(TEST regression_runtime_reporting_measured_case_lines_use___line__ APPEND PROPERTY LABELS "cmake")
unset(_gentest_measured_line_files_arg)
unset(_gentest_measured_line_files)

gentest_add_check_counts(
    NAME regression_orchestrator_fail_fast_blocks_measured
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    PASS 0
    FAIL 1
    SKIP 0
    ARGS --fail-fast)

gentest_add_check_contains(
    NAME regression_orchestrator_fail_fast_blocks_measured_lists_bench
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    EXPECT_SUBSTRING "regressions/orchestrator_fail_fast_blocks_measured/bench_should_not_run"
    ARGS --list-benches)

gentest_add_check_contains(
    NAME regression_orchestrator_fail_fast_blocks_measured_lists_jitter
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    EXPECT_SUBSTRING "regressions/orchestrator_fail_fast_blocks_measured/jitter_should_not_run"
    ARGS --list-benches)

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocks_measured_no_bench_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=orchestrator-fail-fast-test-failure"
        "FORBID_SUBSTRING=orchestrator-fail-fast-bench-ran")

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocks_measured_no_jitter_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=orchestrator-fail-fast-test-failure"
        "FORBID_SUBSTRING=orchestrator-fail-fast-jitter-ran")

gentest_add_check_exit_code(
    NAME regression_bench_setup_skip_noninfra_exit_zero
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_RC 0
    ARGS --run=regressions/bench_setup_skip_should_not_fail --kind=bench)

gentest_add_run_and_check_file(
    NAME regression_bench_setup_skip_noninfra_junit_skip
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_setup_skip_noninfra_junit.xml
    EXPECT_SUBSTRING "skipped=\"1\""
    EXPECT_RC 0
    ARGS --run=regressions/bench_setup_skip_should_not_fail --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_setup_skip_noninfra_junit.xml)

gentest_add_check_exit_code(
    NAME regression_jitter_setup_skip_noninfra_exit_zero
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_RC 0
    ARGS --run=regressions/jitter_setup_skip_should_not_fail --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_jitter_setup_skip_noninfra_junit_skip
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_setup_skip_noninfra_junit.xml
    EXPECT_SUBSTRING "skipped=\"1\""
    EXPECT_RC 0
    ARGS --run=regressions/jitter_setup_skip_should_not_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_setup_skip_noninfra_junit.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_partial_setup_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_partial_setup_teardown/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-second-setup-failed"
        "FORBID_SUBSTRINGS=jitter-second-setup-failed")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_partial_setup_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_partial_setup_teardown/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-second-setup-failed"
        "FORBID_SUBSTRINGS=bench-second-setup-failed")

gentest_add_cmake_script_test(
    NAME regression_generated_bench_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_partial_setup_teardown/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-bench-second-setup-failed"
        "FORBID_SUBSTRINGS=generated-jitter-second-setup-failed|regression marker: generated bench local teardown missing after setup failure")

gentest_add_cmake_script_test(
    NAME regression_generated_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_partial_setup_teardown/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-jitter-second-setup-failed"
        "FORBID_SUBSTRINGS=generated-bench-second-setup-failed|regression marker: generated jitter local teardown missing after setup failure")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_throw_teardown_armed>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_throw_teardown_armed/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-setup-throws"
        "FORBID_SUBSTRINGS=jitter-setup-throws")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_throw_teardown_armed>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_throw_teardown_armed/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-setup-throws"
        "FORBID_SUBSTRINGS=bench-setup-throws")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_teardown_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-teardown-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_call_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-call-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=call+teardown failed for regressions/measured_local_fixture_call_teardown_dualfail/bench")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_call_teardown_dualfail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_failure.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_call_teardown_dualfail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_not_skipped.xml
    EXPECT_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_teardown_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-teardown-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_call_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-call-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=call+teardown failed for regressions/measured_local_fixture_call_teardown_dualfail/jitter")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_failure.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_not_skipped.xml
    EXPECT_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-setup-skip-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=bench-teardown-fail-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=setup+teardown failed for regressions/measured_local_fixture_setup_skip_teardown_fail/bench"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_failure.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_not_skipped.xml
    EXPECT_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-setup-skip-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=jitter-teardown-fail-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=setup+teardown failed for regressions/measured_local_fixture_setup_skip_teardown_fail/jitter"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_failure.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_not_skipped.xml
    EXPECT_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_skip_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/bench --kind=bench
    DEFINES
        "EXPECT_RC=0"
        "EXPECT_SUBSTRING=bench-setup-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_skip_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/bench --kind=bench
    DEFINES
        "EXPECT_RC=0"
        "EXPECT_SUBSTRING=bench-teardown-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_skip_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=0"
        "EXPECT_SUBSTRING=jitter-setup-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_skip_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=0"
        "EXPECT_SUBSTRING=jitter-teardown-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip/teardown skip")

if(GENTEST_ENABLE_ALLURE_TESTS)
    gentest_add_run_and_check_file(
        NAME regression_bench_local_fixture_call_teardown_dualfail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail/result-0-result.json
        EXPECT_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail)

    gentest_add_run_and_check_file(
        NAME regression_jitter_local_fixture_call_teardown_dualfail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail/result-0-result.json
        EXPECT_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail)

    gentest_add_run_and_check_file(
        NAME regression_bench_local_fixture_setup_skip_teardown_fail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_setup_skip_teardown_fail/result-0-result.json
        EXPECT_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_setup_skip_teardown_fail)

    gentest_add_run_and_check_file(
        NAME regression_jitter_local_fixture_setup_skip_teardown_fail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_setup_skip_teardown_fail/result-0-result.json
        EXPECT_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_setup_skip_teardown_fail)
endif()

gentest_add_cmake_script_test(
    NAME regression_generated_bench_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_setup_throw_teardown_armed/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-bench-setup-throws"
        "FORBID_SUBSTRINGS=generated-jitter-setup-throws|regression marker: generated bench teardown not armed before setup")

gentest_add_cmake_script_test(
    NAME regression_generated_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_setup_throw_teardown_armed/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-jitter-setup-throws"
        "FORBID_SUBSTRINGS=generated-bench-setup-throws|regression marker: generated jitter teardown not armed before setup")

gentest_add_cmake_script_test(
    NAME regression_generated_noexceptions_bench_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown_noexc>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_partial_setup_teardown/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-bench-second-setup-failed"
        "FORBID_SUBSTRINGS=generated-jitter-second-setup-failed|regression marker: generated bench local teardown missing after setup failure")

gentest_add_cmake_script_test(
    NAME regression_generated_noexceptions_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown_noexc>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_partial_setup_teardown/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-jitter-second-setup-failed"
        "FORBID_SUBSTRINGS=generated-bench-second-setup-failed|regression marker: generated jitter local teardown missing after setup failure")

gentest_add_cmake_script_test(
    NAME regression_generated_noexceptions_bench_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown_noexc>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_setup_throw_teardown_armed/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-bench-setup-throws"
        "FORBID_SUBSTRINGS=generated-jitter-setup-throws|regression marker: generated bench teardown not armed before setup")

gentest_add_cmake_script_test(
    NAME regression_generated_noexceptions_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown_noexc>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_generated_local_fixture_setup_throw_teardown_armed/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=generated-jitter-setup-throws"
        "FORBID_SUBSTRINGS=generated-bench-setup-throws|regression marker: generated jitter teardown not armed before setup")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_reentry_no_timeout
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_reentry>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoTimeout.cmake"
    ARGS --run=regressions/shared_fixture_reentry_smoke --kind=test
    DEFINES TIMEOUT_SEC=10 EXPECT_RC=0)

gentest_add_check_death(
    NAME regression_shared_fixture_teardown_failure_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    EXPECT_SUBSTRING "intentional shared fixture teardown failure"
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_failure.xml
    EXPECT_SUBSTRING "errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_preserves_case_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_preserves_case_count.xml
    EXPECT_SUBSTRING "tests=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_preserves_case_count.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_detail.xml
    EXPECT_SUBSTRING "fixture teardown failed for regressions::TeardownFailureFixture: intentional shared fixture teardown failure"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_detail.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_teardown_failure_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    EXPECT_SUBSTRING "Summary: passed 1/1; failed 1; skipped 0; xfail 0; xpass 0."
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test)

gentest_add_check_counts(
    NAME regression_local_fixture_teardown_unwind
    PROG $<TARGET_FILE:gentest_regression_local_fixture_teardown>
    PASS 0
    FAIL 1
    SKIP 2)

gentest_add_check_death(
    NAME regression_local_fixture_teardown_noexceptions_fatal_assert_runs_teardown
    PROG $<TARGET_FILE:gentest_regression_local_fixture_teardown_noexceptions>
    EXPECT_SUBSTRING "local-fixture-teardown-noexc-marker"
    ARGS --run=regressions/local_fixture_teardown_noexceptions/fatal_assert)

gentest_add_check_death(
    NAME regression_local_fixture_teardown_noexceptions_fatal_assert_termination_message
    PROG $<TARGET_FILE:gentest_regression_local_fixture_teardown_noexceptions>
    EXPECT_SUBSTRING "terminating after fatal assertion"
    ARGS --run=regressions/local_fixture_teardown_noexceptions/fatal_assert)

if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
    set_tests_properties(
        regression_local_fixture_teardown_noexceptions_fatal_assert_runs_teardown
        regression_local_fixture_teardown_noexceptions_fatal_assert_termination_message
        PROPERTIES DISABLED "$<CONFIG:Debug>")
endif()

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_free
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 2
    EXPECT_RC 1)

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_member
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 2
    EXPECT_RC 1)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_member_junit_reports_failures
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip.xml
    EXPECT_SUBSTRING "failures=\"2\""
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip.xml)

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_fail_fast
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 1
    EXPECT_RC 1
    ARGS --fail-fast)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_junit_reports_failures
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip.xml
    EXPECT_SUBSTRING "failures=\"2\""
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_setup_skip_bench
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "shared fixture unavailable for 'regressions::shared_setup_skip_bench_jitter::NullSuiteFx': fixture allocation returned null"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_bench_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_setup_skip_jitter
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "shared fixture unavailable for 'regressions::shared_setup_skip_bench_jitter::NullGlobalFx': fixture allocation returned null"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_jitter_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter.xml)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_measured_fail_fast_stops_before_jitter
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "EXPECT_SUBSTRING=regressions/shared_setup_skip_bench_jitter/suite_bench"
        "FORBID_SUBSTRING=regressions/shared_setup_skip_bench_jitter/global_jitter")

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_bench
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "shared fixture unavailable for 'regressions::MissingBenchSuiteFixture': fixture not registered"
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench)

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_bench_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "Summary: passed 0/0; failed 1; skipped 0; xfail 0; xpass 0."
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench)

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "shared fixture unavailable for 'regressions::MissingJitterGlobalFixture': fixture not registered"
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter)

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    EXPECT_SUBSTRING "Summary: passed 0/0; failed 1; skipped 0; xfail 0; xpass 0."
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter)

gentest_add_check_death(
    NAME regression_shared_fixture_duplicate_registration_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    EXPECT_SUBSTRING "registered multiple times with conflicting callbacks"
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_duplicate_registration_junit_reports_error
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_duplicate_registration.xml
    EXPECT_SUBSTRING "errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_duplicate_registration.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_duplicate_registration_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    EXPECT_SUBSTRING "Summary: passed 1/1; failed 1; skipped 0; xfail 0; xpass 0."
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_scope_conflict_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_scope_conflict>
    EXPECT_SUBSTRING "registered with conflicting scopes."
    ARGS --run=regressions/shared_fixture_scope_conflict/smoke --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_duplicate_registration_idempotent_accepted
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration_idempotent>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_duplicate_registration_idempotent/smoke --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_ordering_is_stable
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_ordering>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_ordering/uses_b --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_create_throw_skips
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_throw_skip>
    PASS 0
    FAIL 0
    SKIP 1
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_throw_skip/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_create_throw_reports_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_throw_skip>
    EXPECT_SUBSTRING "manual-create-throw"
    ARGS --run=regressions/shared_fixture_manual_create_throw_skip/member_case --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_setup_throw_skips
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_throw_skip>
    PASS 0
    FAIL 0
    SKIP 1
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_throw_skip/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_setup_throw_reports_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_throw_skip>
    EXPECT_SUBSTRING "manual-setup-throw"
    ARGS --run=regressions/shared_fixture_manual_setup_throw_skip/member_case --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_missing_factory_skips
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_missing_factory_skip>
    PASS 0
    FAIL 0
    SKIP 1
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_missing_factory_skip/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_missing_factory_reports_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_missing_factory_skip>
    EXPECT_SUBSTRING "missing factory"
    ARGS --run=regressions/shared_fixture_missing_factory_skip/member_case --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_runtime_registration_during_run_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_runtime_registration_during_run>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_runtime_registration_during_run/late_register --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_runtime_reentry_is_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_runtime_reentry_rejected>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_runtime_reentry_rejected/outer --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_suite_scope_descendant_lookup
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_suite_scope_descendant>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_suite_scope_descendant/member_case --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_suite_scope_most_specific_lookup
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_suite_scope_most_specific>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_suite_scope_most_specific/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_suite_scope_prefix_collision_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_suite_scope_prefix_collision>
    EXPECT_SUBSTRING "fixture not registered"
    ARGS --run=regressions/shared_fixture_suite_scope_prefix_collision/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_teardown_throw_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_teardown_throw_exit>
    EXPECT_SUBSTRING "manual-teardown-throw"
    ARGS --run=regressions/shared_fixture_manual_teardown_throw_exit/smoke --kind=test)

gentest_add_cmake_script_test(
    NAME regression_time_unit_tables
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckTimeUnitTables.cmake")

gentest_add_cmake_script_test(
    NAME regression_time_unit_ns_override
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckTimeUnitNsOverride.cmake")

gentest_add_check_death(
    NAME regression_time_unit_invalid_value
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    EXPECT_SUBSTRING "error: --time-unit must be one of auto,ns"
    ARGS --time-unit=not-a-unit)

gentest_add_check_death(
    NAME regression_time_unit_duplicate_value
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    EXPECT_SUBSTRING "error: duplicate --time-unit"
    ARGS --time-unit=auto --time-unit=ns)
