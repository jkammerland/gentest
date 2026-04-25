# Extracted from tests/CMakeLists.txt to keep the top-level test list manageable.
# Regression binaries (manual runtime registration to isolate runner edge cases)
function(_gentest_add_measured_pair_no_substring_checks)
    set(one_value_args BENCH_NAME JITTER_NAME PROG RUN_PREFIX EXPECT_RC BENCH_SUBSTRING JITTER_SUBSTRING BENCH_EXTRA_FORBID JITTER_EXTRA_FORBID)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    foreach(_gentest_kind IN ITEMS bench jitter)
        if(_gentest_kind STREQUAL "bench")
            set(_gentest_name "${GENTEST_BENCH_NAME}")
            set(_gentest_expect "${GENTEST_BENCH_SUBSTRING}")
            set(_gentest_opposite "${GENTEST_JITTER_SUBSTRING}")
            set(_gentest_extra_forbid "${GENTEST_BENCH_EXTRA_FORBID}")
        else()
            set(_gentest_name "${GENTEST_JITTER_NAME}")
            set(_gentest_expect "${GENTEST_JITTER_SUBSTRING}")
            set(_gentest_opposite "${GENTEST_BENCH_SUBSTRING}")
            set(_gentest_extra_forbid "${GENTEST_JITTER_EXTRA_FORBID}")
        endif()

        set(_gentest_forbid "${_gentest_opposite}")
        if(NOT "${_gentest_extra_forbid}" STREQUAL "")
            if(NOT "${_gentest_forbid}" STREQUAL "")
                string(APPEND _gentest_forbid "|")
            endif()
            string(APPEND _gentest_forbid "${_gentest_extra_forbid}")
        endif()

        gentest_add_cmake_script_test(
            NAME ${_gentest_name}
            PROG ${GENTEST_PROG}
            SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
            ARGS --run=${GENTEST_RUN_PREFIX}/${_gentest_kind} --kind=${_gentest_kind}
            DEFINES
                "EXPECT_RC=${GENTEST_EXPECT_RC}"
                "REQUIRED_SUBSTRING=${_gentest_expect}"
                "FORBID_SUBSTRINGS=${_gentest_forbid}")
    endforeach()
endfunction()

function(_gentest_add_shared_fixture_blocked_reason_regression)
    set(one_value_args NAME_PREFIX PROG RUN REQUIRED_SUBSTRING)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    gentest_add_check_counts(
        NAME ${GENTEST_NAME_PREFIX}_blocked
        PROG ${GENTEST_PROG}
        PASS 0
        FAIL 0
        SKIP 0
        BLOCKED 1
        EXPECT_RC 1
        ARGS --run=${GENTEST_RUN} --kind=test)

    gentest_add_check_death(
        NAME ${GENTEST_NAME_PREFIX}_reports_reason
        PROG ${GENTEST_PROG}
        REQUIRED_SUBSTRING ${GENTEST_REQUIRED_SUBSTRING}
        ARGS --run=${GENTEST_RUN} --kind=test)
endfunction()

set(_gentest_manual_regressions
    "gentest_regression_bench_assert|bench_assert_propagation.cpp"
    "gentest_regression_shared_fixture_reentry|shared_fixture_reentry.cpp"
    "gentest_regression_shared_fixture_teardown_exit|shared_fixture_teardown_exit.cpp"
    "gentest_regression_member_shared_fixture_setup_skip|member_shared_fixture_setup_skip.cpp"
    "gentest_regression_member_shared_fixture_setup_skip_bench_jitter|member_shared_fixture_setup_skip_bench_jitter.cpp"
    "gentest_regression_shared_fixture_duplicate_registration|shared_fixture_duplicate_registration.cpp"
    "gentest_regression_shared_fixture_duplicate_registration_idempotent|shared_fixture_duplicate_registration_idempotent.cpp"
    "gentest_regression_shared_fixture_scope_conflict|shared_fixture_scope_conflict.cpp"
    "gentest_regression_shared_fixture_ordering|shared_fixture_ordering.cpp"
    "gentest_regression_fixture_group_shuffle_invariants|fixture_group_shuffle_invariants.cpp"
    "gentest_regression_shared_fixture_manual_create_throw_skip|shared_fixture_manual_create_throw_skip.cpp"
    "gentest_regression_shared_fixture_manual_create_skip|shared_fixture_manual_create_skip.cpp"
    "gentest_regression_shared_fixture_manual_create_assert_skip|shared_fixture_manual_create_assert_skip.cpp"
    "gentest_regression_shared_fixture_manual_create_expect_skip|shared_fixture_manual_create_expect_skip.cpp"
    "gentest_regression_shared_fixture_manual_create_expect_skip_precedence|shared_fixture_manual_create_expect_skip_precedence.cpp"
    "gentest_regression_shared_fixture_manual_create_expect_throw|shared_fixture_manual_create_expect_throw.cpp"
    "gentest_regression_shared_fixture_manual_create_stale_bench_error|shared_fixture_manual_create_stale_bench_error.cpp"
    "gentest_regression_shared_fixture_manual_setup_assert_skip|shared_fixture_manual_setup_assert_skip.cpp"
    "gentest_regression_shared_fixture_manual_setup_expect_skip_precedence|shared_fixture_manual_setup_expect_skip_precedence.cpp"
    "gentest_regression_shared_fixture_manual_setup_expect_throw|shared_fixture_manual_setup_expect_throw.cpp"
    "gentest_regression_shared_fixture_manual_setup_skip|shared_fixture_manual_setup_skip.cpp"
    "gentest_regression_shared_fixture_manual_setup_throw_skip|shared_fixture_manual_setup_throw_skip.cpp"
    "gentest_regression_shared_fixture_missing_factory_skip|shared_fixture_missing_factory_skip.cpp"
    "gentest_regression_shared_fixture_runtime_registration_during_run|shared_fixture_runtime_registration_during_run.cpp"
    "gentest_regression_shared_fixture_retry_after_failure|shared_fixture_retry_after_failure.cpp"
    "gentest_regression_runtime_case_snapshot_isolated|runtime_case_snapshot_isolated.cpp"
    "gentest_regression_logging_output|logging_output_regressions.cpp"
    "gentest_regression_shared_fixture_runtime_reentry_rejected|shared_fixture_runtime_reentry_rejected.cpp"
    "gentest_regression_shared_fixture_suite_scope_descendant|shared_fixture_suite_scope_descendant.cpp"
    "gentest_regression_shared_fixture_suite_scope_most_specific|shared_fixture_suite_scope_most_specific.cpp"
    "gentest_regression_shared_fixture_global_scope_prefix_collision|shared_fixture_global_scope_prefix_collision.cpp"
    "gentest_regression_shared_fixture_global_scope_most_specific|shared_fixture_global_scope_most_specific.cpp"
    "gentest_regression_shared_fixture_suite_scope_prefix_collision|shared_fixture_suite_scope_prefix_collision.cpp"
    "gentest_regression_shared_fixture_manual_teardown_throw_exit|shared_fixture_manual_teardown_throw_exit.cpp"
    "gentest_regression_shared_fixture_manual_teardown_assert_exit|shared_fixture_manual_teardown_assert_exit.cpp"
    "gentest_regression_shared_fixture_manual_teardown_expect_throw_exit|shared_fixture_manual_teardown_expect_throw_exit.cpp"
    "gentest_regression_shared_fixture_manual_teardown_skip_exit|shared_fixture_manual_teardown_skip_exit.cpp"
    "gentest_regression_cli_suffix_ambiguity|cli_suffix_ambiguity.cpp"
    "gentest_regression_orchestrator_fail_fast_blocks_measured|orchestrator_fail_fast_blocks_measured.cpp"
    "gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked|orchestrator_fail_fast_blocks_measured_after_blocked.cpp"
    "gentest_regression_measured_local_fixture_partial_setup_teardown|measured_local_fixture_partial_setup_teardown.cpp"
    "gentest_regression_measured_local_fixture_setup_assert_teardown_armed|measured_local_fixture_setup_assert_teardown_armed.cpp"
    "gentest_regression_measured_local_fixture_setup_expect_throw|measured_local_fixture_setup_expect_throw.cpp"
    "gentest_regression_measured_local_fixture_setup_throw_teardown_armed|measured_local_fixture_setup_throw_teardown_armed.cpp"
    "gentest_regression_measured_local_fixture_call_teardown_dualfail|measured_local_fixture_call_teardown_dualfail.cpp"
    "gentest_regression_measured_local_fixture_setup_skip_teardown_fail|measured_local_fixture_setup_skip_teardown_fail.cpp"
    "gentest_regression_measured_local_fixture_setup_skip_teardown_skip|measured_local_fixture_setup_skip_teardown_skip.cpp"
    "gentest_regression_measured_local_fixture_setup_failure_then_skip|measured_local_fixture_setup_failure_then_skip.cpp"
    "gentest_regression_measured_local_fixture_teardown_skip|measured_local_fixture_teardown_skip.cpp"
    "gentest_regression_time_unit_scaling|time_unit_scaling.cpp"
    "gentest_regression_runtime_reporting|runtime_reporting_regressions.cpp"
    "gentest_regression_runtime_selection|runtime_selection_regressions.cpp"
    "gentest_regression_reporting_attachment_collision|reporting_attachment_collision.cpp")

foreach(_gentest_manual_regression IN LISTS _gentest_manual_regressions)
    string(REPLACE "|" ";" _gentest_manual_regression_fields "${_gentest_manual_regression}")
    list(GET _gentest_manual_regression_fields 0 _gentest_manual_target)
    list(GET _gentest_manual_regression_fields 1 _gentest_manual_source)

    gentest_add_manual_regression(
        TARGET ${_gentest_manual_target}
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/regressions/${_gentest_manual_source})
endforeach()
unset(_gentest_manual_regressions)
unset(_gentest_manual_regression)
unset(_gentest_manual_regression_fields)
unset(_gentest_manual_target)
unset(_gentest_manual_source)

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

gentest_add_suite(regression_shared_fixture_manual_free_blocked
    TARGET gentest_regression_shared_fixture_manual_free_blocked
    CASES ${CMAKE_CURRENT_SOURCE_DIR}/regressions/shared_fixture_manual_free_blocked.cpp
    OUTPUT_DIR regressions/shared_fixture_manual_free_blocked
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

gentest_add_cmake_script_test(
    NAME regression_artifact_manifest_schema_matches_validator_contract
    NO_EMULATOR
    PROG ${CMAKE_COMMAND}
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/CheckArtifactManifestSchemaContract.cmake"
    DEFINES
        "SCHEMA=${PROJECT_SOURCE_DIR}/docs/schemas/gentest.artifact_manifest.v1.schema.json"
        "STORY=${PROJECT_SOURCE_DIR}/docs/stories/034_codegen_owned_artifact_manifest_and_module_registration.md")

gentest_add_cmake_script_test(
    NAME regression_check_death_required_substrings_literal_semicolon
    NO_EMULATOR
    PROG ${CMAKE_COMMAND}
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/CheckDeathLiteralSemicolonRegression.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/regression_check_death_required_substrings_literal_semicolon"
        "GENTEST_TESTS_MODULE=${PROJECT_SOURCE_DIR}/cmake/GentestTests.cmake")

gentest_add_cmake_script_test(
    NAME regression_module_artifact_manifest_asserts_exact_generated_paths
    NO_EMULATOR
    PROG ${CMAKE_COMMAND}
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/CheckModuleArtifactManifestAssertionExactPaths.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/regression_module_artifact_manifest_asserts_exact_generated_paths"
        "MODULE_ASSERTIONS=${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/ModuleArtifactManifestAssertions.cmake")

gentest_add_cmake_script_test(
    NAME regression_llvm_config_probe_ignores_unsupported_unversioned
    NO_EMULATOR
    PROG ${CMAKE_COMMAND}
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/CheckLlvmConfigMinimumProbe.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}/regression_llvm_config_probe_ignores_unsupported_unversioned"
        "SHIMS_MODULE=${PROJECT_SOURCE_DIR}/cmake/GentestLlvmDependencyShims.cmake")

gentest_add_check_death(
    NAME regression_bench_assert_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "benchmark call failed for regressions/bench_assert_should_fail"
    ARGS --run=regressions/bench_assert_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_assert_failure_reports_reason
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "intentional benchmark assertion failure"
    ARGS --run=regressions/bench_assert_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_jitter_assert_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "jitter call failed for regressions/jitter_assert_should_fail"
    ARGS --run=regressions/jitter_assert_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_assert_failure_reports_reason
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "intentional jitter assertion failure"
    ARGS --run=regressions/jitter_assert_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_bench_fatal_assert_failure_reports_detail
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "intentional fatal benchmark assertion failure"
    ARGS --run=regressions/bench_fatal_assert_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_fatal_assert_failure_reports_location
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "failed at tests/regressions/bench_assert_propagation.cpp:"
    ARGS --run=regressions/bench_fatal_assert_should_fail --kind=bench)

gentest_add_run_and_check_file(
    NAME regression_bench_fatal_assert_failure_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_fatal_assert_failure_detail.xml
    REQUIRED_SUBSTRING "intentional fatal benchmark assertion failure"
    EXPECT_RC 1
    ARGS --run=regressions/bench_fatal_assert_should_fail --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_fatal_assert_failure_detail.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_fatal_assert_failure_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_fatal_assert_failure_location.xml
    REQUIRED_SUBSTRING "tests/regressions/bench_assert_propagation.cpp:"
    EXPECT_RC 1
    ARGS --run=regressions/bench_fatal_assert_should_fail --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_fatal_assert_failure_location.xml)

gentest_add_check_death(
    NAME regression_bench_std_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "benchmark call failed for regressions/bench_std_exception_should_fail"
    ARGS --run=regressions/bench_std_exception_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_fail_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "benchmark call failed for regressions/bench_fail_should_fail"
    ARGS --run=regressions/bench_fail_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_bench_skip_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "benchmark call failed for regressions/bench_skip_should_fail"
    ARGS --run=regressions/bench_skip_should_fail --kind=bench)

gentest_add_check_death(
    NAME regression_jitter_std_exception_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "jitter call failed for regressions/jitter_std_exception_should_fail"
    ARGS --run=regressions/jitter_std_exception_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_fail_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "jitter call failed for regressions/jitter_fail_should_fail"
    ARGS --run=regressions/jitter_fail_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_fatal_assert_failure_reports_detail
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "intentional fatal jitter assertion failure"
    ARGS --run=regressions/jitter_fatal_assert_should_fail --kind=jitter)

gentest_add_check_death(
    NAME regression_jitter_fatal_assert_failure_reports_location
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "failed at tests/regressions/bench_assert_propagation.cpp:"
    ARGS --run=regressions/jitter_fatal_assert_should_fail --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_jitter_fatal_assert_failure_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_fatal_assert_failure_detail.xml
    REQUIRED_SUBSTRING "intentional fatal jitter assertion failure"
    EXPECT_RC 1
    ARGS --run=regressions/jitter_fatal_assert_should_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_fatal_assert_failure_detail.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_fatal_assert_failure_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_fatal_assert_failure_location.xml
    REQUIRED_SUBSTRING "tests/regressions/bench_assert_propagation.cpp:"
    EXPECT_RC 1
    ARGS --run=regressions/jitter_fatal_assert_should_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_fatal_assert_failure_location.xml)

gentest_add_check_death(
    NAME regression_jitter_skip_failure_propagates
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    REQUIRED_SUBSTRING "jitter call failed for regressions/jitter_skip_should_fail"
    ARGS --run=regressions/jitter_skip_should_fail --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_runtime_reporting_fallback_assertion_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_fallback_assertion.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/runtime_reporting/fallback_assertion_failure --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_fallback_assertion.xml)

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_github_annotation_escapes_file_title
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/runtime_reporting/gha,title:punct --kind=test --github-annotations
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=::error file=C%3A/repo%2Cwin/src/runtime_reporting_case.cpp,line=77,title=regressions/runtime_reporting/gha%2Ctitle%3Apunct::"
        "FORBID_SUBSTRING=::error file=C:/repo,win/src/runtime_reporting_case.cpp,line=77,title=regressions/runtime_reporting/gha,title:punct::")

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_junit_cdata_token_split
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckRuntimeReportingCdata.cmake"
    ARGS --run=regressions/runtime_reporting/junit_cdata_close_token_failure --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_cdata_token.xml
    DEFINES
        "JUNIT_FILE=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_cdata_token.xml"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_junit_xml_control_sanitized
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckRuntimeReportingXmlSanitize.cmake"
    ARGS --run=regressions/runtime_reporting/junit_invalid_xml_control_failure --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_xml_control.xml
    DEFINES
        "JUNIT_FILE=${CMAKE_CURRENT_BINARY_DIR}/runtime_reporting_xml_control.xml"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_runtime_reporting_junit_open_failure_is_visible
    PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/runtime_reporting/pass_for_junit_io_visibility --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=failed to open JUnit report:"
        "EXPECT_COUNT_SUBSTRING=failed to open JUnit report:"
        "EXPECT_COUNT=1")

gentest_add_cmake_script_test(
    NAME regression_runtime_selection_mixed_summary_counts
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --filter=regressions/runtime_selection/mixed_summary_*
        --bench-epochs=1
        --bench-warmup=0
        --bench-min-epoch-time-s=0
        --bench-min-total-time-s=0
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=Summary: passed 2/2")

gentest_add_cmake_script_test(
    NAME regression_runtime_selection_duplicate_name_summary_first_location
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --filter=regressions/runtime_selection/duplicate_name --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=runtime_selection_regressions.cpp:17")

gentest_add_cmake_script_test(
    NAME regression_runtime_selection_duplicate_name_summary_second_location
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --filter=regressions/runtime_selection/duplicate_name --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=runtime_selection_regressions.cpp:20")

gentest_add_check_contains(
    NAME regression_runtime_selection_list_death
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    REQUIRED_SUBSTRING regressions/runtime_selection/death_case
    ARGS --list-death)

gentest_add_check_death(
    NAME regression_runtime_selection_exact_death_requires_opt_in
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    REQUIRED_SUBSTRING "Case 'regressions/runtime_selection/death_case' is tagged as a death test; rerun with --include-death"
    ARGS --run=regressions/runtime_selection/death_case --kind=test)

gentest_add_check_counts(
    NAME regression_runtime_selection_exact_death_opt_in
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/runtime_selection/death_case --kind=test --include-death)

gentest_add_cmake_script_test(
    NAME regression_runtime_selection_positive_bench_table
    PROG $<TARGET_FILE:gentest_regression_runtime_selection>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/runtime_selection/bench_table_case
        --kind=bench
        --bench-table
        --bench-epochs=1
        --bench-warmup=0
        --bench-min-epoch-time-s=0
        --bench-min-total-time-s=0
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=regressions/runtime_selection/bench_table_case")

gentest_add_cmake_script_test(
    NAME regression_logging_output_on_failure_policy_pass_silent
    PROG $<TARGET_FILE:gentest_regression_logging_output>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/logging_output/on_failure_policy_pass_silent --kind=test
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=Summary: passed 1/1; failed 0; skipped 0; blocked 0; xfail 0; xpass 0."
        "FORBID_SUBSTRING=failure-only hidden on pass")

gentest_add_cmake_script_test(
    NAME regression_logging_output_always_policy_visible_on_pass
    PROG $<TARGET_FILE:gentest_regression_logging_output>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/logging_output/always_policy_visible_on_pass --kind=test
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=always-policy visible on pass"
        "EXPECT_COUNT_SUBSTRING=always-policy visible on pass"
        "EXPECT_COUNT=1")

gentest_add_cmake_script_test(
    NAME regression_logging_output_default_always_policy_visible_on_pass
    PROG $<TARGET_FILE:gentest_regression_logging_output>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/logging_output/default_always_policy_visible_on_pass --kind=test
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=default-always visible on pass"
        "EXPECT_COUNT_SUBSTRING=default-always visible on pass"
        "EXPECT_COUNT=1")

gentest_add_cmake_script_test(
    NAME regression_logging_output_explicit_never_overrides_default_always
    PROG $<TARGET_FILE:gentest_regression_logging_output>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/logging_output/explicit_never_overrides_default_always --kind=test
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=Summary: passed 1/1; failed 0; skipped 0; blocked 0; xfail 0; xpass 0."
        "FORBID_SUBSTRING=default-always overridden by explicit never")

set(_gentest_measured_line_files
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/bench_assert_propagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_call_teardown_dualfail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_partial_setup_teardown.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_assert_teardown_armed.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_expect_throw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_failure_then_skip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_fail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_skip_teardown_skip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_setup_throw_teardown_armed.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/regressions/measured_local_fixture_teardown_skip.cpp")
string(JOIN "|" _gentest_measured_line_files_arg ${_gentest_measured_line_files})
add_test(NAME regression_runtime_reporting_measured_case_lines_use___line__
    COMMAND ${CMAKE_COMMAND}
        -DFILES=${_gentest_measured_line_files_arg}
        -P ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoLiteralCaseLines.cmake)
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
    REQUIRED_SUBSTRING "regressions/orchestrator_fail_fast_blocks_measured/bench_should_not_run"
    ARGS --list-benches)

gentest_add_check_contains(
    NAME regression_orchestrator_fail_fast_blocks_measured_lists_jitter
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    REQUIRED_SUBSTRING "regressions/orchestrator_fail_fast_blocks_measured/jitter_should_not_run"
    ARGS --list-benches)

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocks_measured_no_bench_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=orchestrator-fail-fast-test-failure"
        "FORBID_SUBSTRING=orchestrator-fail-fast-bench-phase-ran")

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocks_measured_no_jitter_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=orchestrator-fail-fast-test-failure"
        "FORBID_SUBSTRING=orchestrator-fail-fast-jitter-phase-ran")

gentest_add_check_counts(
    NAME regression_orchestrator_fail_fast_blocked_blocks_measured
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 1
    EXPECT_RC 1
    ARGS --fail-fast)

gentest_add_check_contains(
    NAME regression_orchestrator_fail_fast_blocked_blocks_measured_lists_bench
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked>
    REQUIRED_SUBSTRING "regressions/orchestrator_fail_fast_blocked_blocks_measured/bench_should_not_run"
    ARGS --list-benches)

gentest_add_check_contains(
    NAME regression_orchestrator_fail_fast_blocked_blocks_measured_lists_jitter
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked>
    REQUIRED_SUBSTRING "regressions/orchestrator_fail_fast_blocked_blocks_measured/jitter_should_not_run"
    ARGS --list-benches)

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocked_blocks_measured_no_bench_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=orchestrator-fail-fast-blocked-fixture"
        "FORBID_SUBSTRING=orchestrator-fail-fast-blocked-bench-phase-ran")

gentest_add_cmake_script_test(
    NAME regression_orchestrator_fail_fast_blocked_blocks_measured_no_jitter_call
    PROG $<TARGET_FILE:gentest_regression_orchestrator_fail_fast_blocks_measured_after_blocked>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=orchestrator-fail-fast-blocked-fixture"
        "FORBID_SUBSTRING=orchestrator-fail-fast-blocked-jitter-phase-ran")

gentest_add_check_exit_code(
    NAME regression_bench_setup_skip_noninfra_exit_zero
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    EXPECT_RC 0
    ARGS --run=regressions/bench_setup_skip_should_not_fail --kind=bench)

gentest_add_run_and_check_file(
    NAME regression_bench_setup_skip_noninfra_junit_skip
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_setup_skip_noninfra_junit.xml
    REQUIRED_SUBSTRING "skipped=\"1\""
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
    REQUIRED_SUBSTRING "skipped=\"1\""
    EXPECT_RC 0
    ARGS --run=regressions/jitter_setup_skip_should_not_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_setup_skip_noninfra_junit.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_calibration_assert_stops_after_calibration
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/bench_calibration_assert_should_stop_after_calibration
        --kind=bench
        --bench-warmup=1
        --bench-epochs=1
        --bench-min-total-time-s=0
        --bench-min-epoch-time-s=0
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=benchmark call failed for regressions/bench_calibration_assert_should_stop_after_calibration"
        "FORBID_SUBSTRING=regression marker: benchmark continued after calibration failure")

gentest_add_cmake_script_test(
    NAME regression_jitter_calibration_assert_stops_after_calibration
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/jitter_calibration_assert_should_stop_after_calibration
        --kind=jitter
        --bench-warmup=1
        --bench-epochs=1
        --bench-min-total-time-s=0
        --bench-min-epoch-time-s=0
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=jitter call failed for regressions/jitter_calibration_assert_should_stop_after_calibration"
        "FORBID_SUBSTRING=regression marker: jitter continued after calibration failure")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_partial_setup_failure_teardown
    JITTER_NAME regression_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_partial_setup_teardown>
    RUN_PREFIX regressions/measured_local_fixture_partial_setup_teardown
    EXPECT_RC 1
    BENCH_SUBSTRING bench-second-setup-failed
    JITTER_SUBSTRING jitter-second-setup-failed)

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_generated_bench_local_fixture_partial_setup_failure_teardown
    JITTER_NAME regression_generated_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown>
    RUN_PREFIX regressions/measured_generated_local_fixture_partial_setup_teardown
    EXPECT_RC 1
    BENCH_SUBSTRING generated-bench-second-setup-failed
    JITTER_SUBSTRING generated-jitter-second-setup-failed
    BENCH_EXTRA_FORBID "regression marker: generated bench local teardown missing after setup failure"
    JITTER_EXTRA_FORBID "regression marker: generated jitter local teardown missing after setup failure")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_setup_assert_teardown_armed
    JITTER_NAME regression_jitter_local_fixture_setup_assert_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_assert_teardown_armed>
    RUN_PREFIX regressions/measured_local_fixture_setup_assert_teardown_armed
    EXPECT_RC 1
    BENCH_SUBSTRING bench-setup-fatal-assert-marker
    JITTER_SUBSTRING jitter-setup-fatal-assert-marker
    BENCH_EXTRA_FORBID "regression marker: bench call executed after setup assert"
    JITTER_EXTRA_FORBID "regression marker: jitter call executed after setup assert")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_assert_teardown_armed_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_assert_teardown_armed>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_assert_teardown_armed_detail.xml
    REQUIRED_SUBSTRING "bench-setup-fatal-assert-marker"
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_assert_teardown_armed/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_assert_teardown_armed_detail.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_assert_teardown_armed_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_assert_teardown_armed>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_assert_teardown_armed_location.xml
    REQUIRED_SUBSTRING "tests/regressions/measured_local_fixture_setup_assert_teardown_armed.cpp:"
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_assert_teardown_armed/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_assert_teardown_armed_location.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_assert_teardown_armed_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_assert_teardown_armed>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_assert_teardown_armed_detail.xml
    REQUIRED_SUBSTRING "jitter-setup-fatal-assert-marker"
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_assert_teardown_armed/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_assert_teardown_armed_detail.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_assert_teardown_armed_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_assert_teardown_armed>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_assert_teardown_armed_location.xml
    REQUIRED_SUBSTRING "tests/regressions/measured_local_fixture_setup_assert_teardown_armed.cpp:"
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_assert_teardown_armed/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_assert_teardown_armed_location.xml)

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_setup_throw_teardown_armed
    JITTER_NAME regression_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_throw_teardown_armed>
    RUN_PREFIX regressions/measured_local_fixture_setup_throw_teardown_armed
    EXPECT_RC 1
    BENCH_SUBSTRING bench-setup-throws
    JITTER_SUBSTRING jitter-setup-throws)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_teardown_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=bench-teardown-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_call_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=bench-call-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=call+teardown failed for regressions/measured_local_fixture_call_teardown_dualfail/bench")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_call_teardown_dualfail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_failure.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_call_teardown_dualfail_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_time.xml
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/measured_local_fixture_call_teardown_dualfail/bench"
        "REQUIRED_SUBSTRING=failures=\"1\""
        "EXPECT_RC=1")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_call_teardown_dualfail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRINGS "<skipped" "skipped=\"1\""
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_call_teardown_dualfail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_teardown_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=jitter-teardown-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_call_detail
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=jitter-call-phase-failure-marker")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=call+teardown failed for regressions/measured_local_fixture_call_teardown_dualfail/jitter")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_failure.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_time.xml
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/measured_local_fixture_call_teardown_dualfail/jitter"
        "REQUIRED_SUBSTRING=failures=\"1\""
        "EXPECT_RC=1")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_call_teardown_dualfail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRINGS "<skipped" "skipped=\"1\""
    ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_call_teardown_dualfail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=bench-setup-skip-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=bench-teardown-fail-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=setup+teardown failed for regressions/measured_local_fixture_setup_skip_teardown_fail/bench"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_skip_teardown_fail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRING "<skipped"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_skip_teardown_fail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=jitter-setup-skip-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=jitter-teardown-fail-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_reports_phase_classification
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=setup+teardown failed for regressions/measured_local_fixture_setup_skip_teardown_fail/jitter"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_skip_teardown_fail_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRING "<skipped"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_skip_teardown_fail_not_skipped.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_skip_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/bench --kind=bench
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=bench-setup-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_setup_skip_teardown_skip_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/bench --kind=bench
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=bench-teardown-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: bench call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_skip_reports_setup_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=jitter-setup-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip/teardown skip")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_setup_skip_teardown_skip_reports_teardown_issue
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_skip/jitter --kind=jitter
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=jitter-teardown-skip-only-marker"
        "FORBID_SUBSTRINGS=regression marker: jitter call executed after setup skip/teardown skip")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_setup_failure_then_skip_reports_failure
    JITTER_NAME regression_jitter_local_fixture_setup_failure_then_skip_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_failure_then_skip>
    RUN_PREFIX regressions/measured_local_fixture_setup_failure_then_skip
    EXPECT_RC 1
    BENCH_SUBSTRING "bench-setup-failure-before-skip-marker"
    JITTER_SUBSTRING "jitter-setup-failure-before-skip-marker"
    BENCH_EXTRA_FORBID "bench-setup-skip-after-failure-marker|[ SKIP ] regressions/measured_local_fixture_setup_failure_then_skip/bench"
    JITTER_EXTRA_FORBID "jitter-setup-skip-after-failure-marker|[ SKIP ] regressions/measured_local_fixture_setup_failure_then_skip/jitter")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_setup_expect_throw_reports_throw
    JITTER_NAME regression_jitter_local_fixture_setup_expect_throw_reports_throw
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_expect_throw>
    RUN_PREFIX regressions/measured_local_fixture_setup_expect_throw
    EXPECT_RC 1
    BENCH_SUBSTRING "std::exception: bench-setup-throw-after-expect-marker"
    JITTER_SUBSTRING "std::exception: jitter-setup-throw-after-expect-marker"
    BENCH_EXTRA_FORBID "regression marker: bench call executed after setup expect+throw"
    JITTER_EXTRA_FORBID "regression marker: jitter call executed after setup expect+throw")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_failure_then_skip_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_failure_then_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_failure_then_skip_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_failure_then_skip/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_failure_then_skip_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_setup_failure_then_skip_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_failure_then_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_failure_then_skip_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRING "<skipped"
    ARGS --run=regressions/measured_local_fixture_setup_failure_then_skip/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_setup_failure_then_skip_not_skipped.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_failure_then_skip_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_failure_then_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_failure_then_skip_failure.xml
    REQUIRED_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/measured_local_fixture_setup_failure_then_skip/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_failure_then_skip_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_setup_failure_then_skip_junit_not_skipped
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_failure_then_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_failure_then_skip_not_skipped.xml
    REQUIRED_SUBSTRING "skipped=\"0\""
    EXPECT_RC 1
    FORBID_SUBSTRING "<skipped"
    ARGS --run=regressions/measured_local_fixture_setup_failure_then_skip/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_setup_failure_then_skip_not_skipped.xml)

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_bench_local_fixture_teardown_skip_reports_reason
    JITTER_NAME regression_jitter_local_fixture_teardown_skip_reports_reason
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    RUN_PREFIX regressions/measured_local_fixture_teardown_skip
    EXPECT_RC 0
    BENCH_SUBSTRING bench-teardown-skip-only-marker
    JITTER_SUBSTRING jitter-teardown-skip-only-marker
    BENCH_EXTRA_FORBID "(0 ms)"
    JITTER_EXTRA_FORBID "(0 ms)")

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_teardown_skip_zero_budget_still_reports_nonzero_ms
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/measured_local_fixture_teardown_skip/bench
        --kind=bench
        --bench-min-epoch-time-s=0
        --bench-min-total-time-s=0
        --bench-max-total-time-s=0
        --bench-warmup=0
        --bench-epochs=1
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=bench-teardown-skip-only-marker"
        "FORBID_SUBSTRING=(0 ms)")

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_teardown_skip_zero_budget_still_reports_nonzero_ms
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS
        --run=regressions/measured_local_fixture_teardown_skip/jitter
        --kind=jitter
        --bench-min-epoch-time-s=0
        --bench-min-total-time-s=0
        --bench-max-total-time-s=0
        --bench-warmup=0
        --bench-epochs=1
    DEFINES
        "EXPECT_RC=0"
        "REQUIRED_SUBSTRING=jitter-teardown-skip-only-marker"
        "FORBID_SUBSTRING=(0 ms)")

gentest_add_run_and_check_file(
    NAME regression_bench_local_fixture_teardown_skip_junit_reports_skip
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_teardown_skip_reports_skip.xml
    REQUIRED_SUBSTRING "skipped=\"1\""
    EXPECT_RC 0
    ARGS --run=regressions/measured_local_fixture_teardown_skip/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_teardown_skip_reports_skip.xml)

gentest_add_cmake_script_test(
    NAME regression_bench_local_fixture_teardown_skip_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/measured_local_fixture_teardown_skip/bench --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_teardown_skip_time.xml
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/bench_local_fixture_teardown_skip_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/measured_local_fixture_teardown_skip/bench"
        "REQUIRED_SUBSTRING=skipped=\"1\""
        "EXPECT_RC=0")

gentest_add_run_and_check_file(
    NAME regression_jitter_local_fixture_teardown_skip_junit_reports_skip
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_teardown_skip_reports_skip.xml
    REQUIRED_SUBSTRING "skipped=\"1\""
    EXPECT_RC 0
    ARGS --run=regressions/measured_local_fixture_teardown_skip/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_teardown_skip_reports_skip.xml)

gentest_add_cmake_script_test(
    NAME regression_jitter_local_fixture_teardown_skip_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/measured_local_fixture_teardown_skip/jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_teardown_skip_time.xml
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/jitter_local_fixture_teardown_skip_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/measured_local_fixture_teardown_skip/jitter"
        "REQUIRED_SUBSTRING=skipped=\"1\""
        "EXPECT_RC=0")

if(GENTEST_ENABLE_ALLURE_TESTS)
    gentest_add_cmake_script_test(
        NAME regression_bench_local_fixture_teardown_skip_allure_reports_skip
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
        ARGS --run=regressions/measured_local_fixture_teardown_skip/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_teardown_skip
        DEFINES
            "FILE=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_teardown_skip/result-0-result.json"
            "FORMAT=ALLURE"
            "REQUIRED_SUBSTRING=\"status\":\"skipped\""
            "EXPECT_RC=0")

    gentest_add_cmake_script_test(
        NAME regression_jitter_local_fixture_teardown_skip_allure_reports_skip
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_teardown_skip>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
        ARGS --run=regressions/measured_local_fixture_teardown_skip/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_teardown_skip
        DEFINES
            "FILE=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_teardown_skip/result-0-result.json"
            "FORMAT=ALLURE"
            "REQUIRED_SUBSTRING=\"status\":\"skipped\""
            "EXPECT_RC=0")

    gentest_add_run_and_check_file(
        NAME regression_bench_local_fixture_call_teardown_dualfail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail/result-0-result.json
        REQUIRED_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail)

    gentest_add_cmake_script_test(
        NAME regression_bench_local_fixture_call_teardown_dualfail_allure_preserves_time
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail
        DEFINES
            "FILE=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_call_teardown_dualfail/result-0-result.json"
            "FORMAT=ALLURE"
            "REQUIRED_SUBSTRING=\"status\":\"failed\""
            "EXPECT_RC=1")

    gentest_add_run_and_check_file(
        NAME regression_jitter_local_fixture_call_teardown_dualfail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail/result-0-result.json
        REQUIRED_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail)

    gentest_add_cmake_script_test(
        NAME regression_jitter_local_fixture_call_teardown_dualfail_allure_preserves_time
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_call_teardown_dualfail>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
        ARGS --run=regressions/measured_local_fixture_call_teardown_dualfail/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail
        DEFINES
            "FILE=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_call_teardown_dualfail/result-0-result.json"
            "FORMAT=ALLURE"
            "REQUIRED_SUBSTRING=\"status\":\"failed\""
            "EXPECT_RC=1")

    gentest_add_run_and_check_file(
        NAME regression_bench_local_fixture_setup_skip_teardown_fail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_setup_skip_teardown_fail/result-0-result.json
        REQUIRED_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/bench --kind=bench --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_bench_local_fixture_setup_skip_teardown_fail)

    gentest_add_run_and_check_file(
        NAME regression_jitter_local_fixture_setup_skip_teardown_fail_allure_reports_failure
        PROG $<TARGET_FILE:gentest_regression_measured_local_fixture_setup_skip_teardown_fail>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_setup_skip_teardown_fail/result-0-result.json
        REQUIRED_SUBSTRING "\"status\":\"failed\""
        EXPECT_RC 1
        ARGS --run=regressions/measured_local_fixture_setup_skip_teardown_fail/jitter --kind=jitter --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_jitter_local_fixture_setup_skip_teardown_fail)
endif()

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_generated_bench_local_fixture_setup_throw_teardown_armed
    JITTER_NAME regression_generated_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown>
    RUN_PREFIX regressions/measured_generated_local_fixture_setup_throw_teardown_armed
    EXPECT_RC 1
    BENCH_SUBSTRING generated-bench-setup-throws
    JITTER_SUBSTRING generated-jitter-setup-throws
    BENCH_EXTRA_FORBID "regression marker: generated bench teardown not armed before setup"
    JITTER_EXTRA_FORBID "regression marker: generated jitter teardown not armed before setup")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_generated_noexceptions_bench_local_fixture_partial_setup_failure_teardown
    JITTER_NAME regression_generated_noexceptions_jitter_local_fixture_partial_setup_failure_teardown
    PROG $<TARGET_FILE:gentest_regression_mg_lf_partial_teardown_noexc>
    RUN_PREFIX regressions/measured_generated_local_fixture_partial_setup_teardown
    EXPECT_RC 1
    BENCH_SUBSTRING generated-bench-second-setup-failed
    JITTER_SUBSTRING generated-jitter-second-setup-failed
    BENCH_EXTRA_FORBID "regression marker: generated bench local teardown missing after setup failure"
    JITTER_EXTRA_FORBID "regression marker: generated jitter local teardown missing after setup failure")

_gentest_add_measured_pair_no_substring_checks(
    BENCH_NAME regression_generated_noexceptions_bench_local_fixture_setup_throw_teardown_armed
    JITTER_NAME regression_generated_noexceptions_jitter_local_fixture_setup_throw_teardown_armed
    PROG $<TARGET_FILE:gentest_regression_mg_lf_setup_throw_teardown_noexc>
    RUN_PREFIX regressions/measured_generated_local_fixture_setup_throw_teardown_armed
    EXPECT_RC 1
    BENCH_SUBSTRING generated-bench-setup-throws
    JITTER_SUBSTRING generated-jitter-setup-throws
    BENCH_EXTRA_FORBID "regression marker: generated bench teardown not armed before setup"
    JITTER_EXTRA_FORBID "regression marker: generated jitter teardown not armed before setup")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_reentry_no_timeout
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_reentry>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoTimeout.cmake"
    ARGS --run=regressions/shared_fixture_reentry_smoke --kind=test
    DEFINES TIMEOUT_SEC=10 EXPECT_RC=0)

gentest_add_check_death(
    NAME regression_shared_fixture_teardown_failure_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    REQUIRED_SUBSTRING "intentional shared fixture teardown failure"
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_reports_failure
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_failure.xml
    REQUIRED_SUBSTRING "errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_failure.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_preserves_case_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_preserves_case_count.xml
    REQUIRED_SUBSTRING "tests=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_preserves_case_count.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_teardown_failure_junit_reports_detail
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_detail.xml
    REQUIRED_SUBSTRING "fixture teardown failed for regressions::TeardownFailureFixture: intentional shared fixture teardown failure"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_teardown_failure_exit --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_teardown_failure_reports_detail.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_teardown_failure_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_teardown_exit>
    REQUIRED_SUBSTRING "Summary: passed 1/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 0."
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
    REQUIRED_SUBSTRING "local-fixture-teardown-noexc-marker"
    ARGS --run=regressions/local_fixture_teardown_noexceptions/fatal_assert)

gentest_add_check_death(
    NAME regression_local_fixture_teardown_noexceptions_fatal_assert_termination_message
    PROG $<TARGET_FILE:gentest_regression_local_fixture_teardown_noexceptions>
    REQUIRED_SUBSTRING "terminating after fatal assertion"
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
    SKIP 0
    BLOCKED 2
    EXPECT_RC 1)

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_member
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 2
    EXPECT_RC 1)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_member_junit_reports_blocked_outcome
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip.xml
    REQUIRED_SUBSTRING "tests=\"2\" failures=\"0\" skipped=\"2\" errors=\"2\""
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_member_junit_reports_suite_case_blocked_element
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_suite_failure_tag.xml
    REQUIRED_SUBSTRING "name=\"regressions/member_shared_setup_skip/suite_member\" time=\"0\">
    <skipped message=\"blocked: fixture allocation failed\"/>"
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_suite_failure_tag.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_member_junit_reports_global_case_blocked_element
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_global_failure_tag.xml
    REQUIRED_SUBSTRING "name=\"regressions/member_shared_setup_skip/global_member\" time=\"0\">
    <skipped message=\"blocked: fixture allocation failed\"/>"
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_global_failure_tag.xml)

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_fail_fast
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 1
    EXPECT_RC 1
    ARGS --fail-fast)

gentest_add_check_counts(
    NAME regression_shared_fixture_setup_skip_member_fail_fast
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 1
    EXPECT_RC 1
    ARGS --fail-fast)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_junit_reports_blocked_outcome
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip.xml
    REQUIRED_SUBSTRING "tests=\"2\" failures=\"0\" skipped=\"2\" errors=\"2\""
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_junit_reports_global_case_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_global_failure_tag.xml
    REQUIRED_SUBSTRING "<skipped message=\"blocked: shared fixture unavailable for 'regressions::shared_setup_skip::NullGlobalFx': fixture allocation returned null\"/>"
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_global_failure_tag.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_junit_reports_suite_case_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_suite_failure_tag.xml
    REQUIRED_SUBSTRING "<skipped message=\"blocked: shared fixture unavailable for 'regressions::shared_setup_skip::NullSuiteFx': fixture allocation returned null\"/>"
    EXPECT_RC 1
    ARGS --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_suite_failure_tag.xml)

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_free_blocked_counts
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 4
    EXPECT_RC 1)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_create_skip_junit
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_skip.xml
    REQUIRED_SUBSTRING "blocked: shared fixture unavailable for 'regressions::manual_free_blocked::CreateSkipFx'"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/create_skip --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_skip.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_create_assert_junit
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_assert.xml
    REQUIRED_SUBSTRING "blocked: shared fixture unavailable for 'regressions::manual_free_blocked::CreateAssertFx'"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/create_assert --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_assert.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_setup_skip_junit
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_skip.xml
    REQUIRED_SUBSTRING "blocked: shared fixture unavailable for 'regressions::manual_free_blocked::SetupSkipFx'"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/setup_skip --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_skip.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_setup_assert_junit
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_assert.xml
    REQUIRED_SUBSTRING "blocked: shared fixture unavailable for 'regressions::manual_free_blocked::SetupAssertFx'"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/setup_assert --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_assert.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_create_skip_junit_preserves_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_skip_reason.xml
    REQUIRED_SUBSTRING "manual-free-create-skip"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/create_skip --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_skip_reason.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_setup_skip_junit_preserves_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_skip_reason.xml
    REQUIRED_SUBSTRING "manual-free-setup-skip"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/setup_skip --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_skip_reason.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_create_assert_junit_preserves_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_assert_reason.xml
    REQUIRED_SUBSTRING "manual-free-create-assert"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/create_assert --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_create_assert_reason.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_free_blocked_setup_assert_junit_preserves_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_free_blocked>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_assert_reason.xml
    REQUIRED_SUBSTRING "manual-free-setup-assert"
    EXPECT_RC 1
    ARGS --run=regressions/manual_free_blocked/setup_assert --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_manual_free_setup_assert_reason.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_setup_skip_bench
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "shared fixture unavailable for 'regressions::shared_setup_skip_bench_jitter::NullSuiteFx': fixture allocation returned null"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_bench_junit_reports_blocked
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench.xml
    REQUIRED_SUBSTRING "tests=\"1\" failures=\"0\" skipped=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench.xml)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_bench_junit_reports_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_not_skipped.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_not_skipped.xml"
        "REQUIRED_SUBSTRING=<skipped message=\"blocked: shared fixture unavailable"
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_bench_junit_reports_blocked_contract
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_blocked_contract.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_blocked_contract.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_bench_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/suite_bench --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_time.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_bench_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/shared_setup_skip_bench_jitter/regressions/shared_setup_skip_bench_jitter/suite_bench"
        "REQUIRED_SUBSTRING=failures=\"0\""
        "EXPECT_RC=1")

gentest_add_check_death(
    NAME regression_shared_fixture_setup_skip_jitter
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "shared fixture unavailable for 'regressions::shared_setup_skip_bench_jitter::NullGlobalFx': fixture allocation returned null"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_setup_skip_jitter_junit_reports_blocked
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter.xml
    REQUIRED_SUBSTRING "tests=\"1\" failures=\"0\" skipped=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter.xml)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_jitter_junit_reports_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_not_skipped.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_not_skipped.xml"
        "REQUIRED_SUBSTRING=<skipped message=\"blocked: shared fixture unavailable"
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_jitter_junit_reports_blocked_contract
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_blocked_contract.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_blocked_contract.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_jitter_junit_preserves_time
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckReportTime.cmake"
    ARGS --run=regressions/shared_setup_skip_bench_jitter/global_jitter --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_time.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_shared_fixture_setup_skip_jitter_time.xml"
        "FORMAT=JUNIT"
        "ITEM_NAME=regressions/shared_setup_skip_bench_jitter/regressions/shared_setup_skip_bench_jitter/global_jitter"
        "REQUIRED_SUBSTRING=failures=\"0\""
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_setup_skip_measured_fail_fast_stops_before_jitter
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --fail-fast
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=regressions/shared_setup_skip_bench_jitter/suite_bench"
        "FORBID_SUBSTRING=regressions/shared_setup_skip_bench_jitter/global_jitter")

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_bench
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "shared fixture unavailable for 'regressions::MissingBenchSuiteFixture': fixture not registered"
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench)

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_bench_reports_blocked_not_failure
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=[ BLOCKED ] regressions/member_shared_setup_skip_measured/bench_member"
        "FORBID_SUBSTRING=[ FAIL ] regressions/member_shared_setup_skip_measured/bench_member")

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_bench_summary_blocked_count
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "Summary: passed 0/1; failed 0; skipped 0; blocked 1; xfail 0; xpass 0."
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench)

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_bench_junit_reports_blocked
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_bench.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_bench.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_bench_junit_reports_blocked_contract
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/bench_member --kind=bench --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_bench_blocked_contract.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_bench_blocked_contract.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "shared fixture unavailable for 'regressions::MissingJitterGlobalFixture': fixture not registered"
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter)

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter_reports_blocked_not_failure
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=[ BLOCKED ] regressions/member_shared_setup_skip_measured/jitter_member"
        "FORBID_SUBSTRING=[ FAIL ] regressions/member_shared_setup_skip_measured/jitter_member")

gentest_add_check_death(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter_summary_blocked_count
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    REQUIRED_SUBSTRING "Summary: passed 0/1; failed 0; skipped 0; blocked 1; xfail 0; xpass 0."
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter)

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter_junit_reports_blocked
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_jitter.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_jitter.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_cmake_script_test(
    NAME regression_member_shared_fixture_setup_skip_measured_jitter_junit_reports_blocked_contract
    PROG $<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/RunAndCheckFile.cmake"
    ARGS --run=regressions/member_shared_setup_skip_measured/jitter_member --kind=jitter --junit=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_jitter_blocked_contract.xml
    DEFINES
        "FILE=${CMAKE_BINARY_DIR}/regression_member_shared_fixture_setup_skip_measured_jitter_blocked_contract.xml"
        "REQUIRED_SUBSTRING=tests=\"1\" failures=\"0\" skipped=\"1\""
        "FORBID_SUBSTRING=<failure"
        "EXPECT_RC=1")

gentest_add_check_death(
    NAME regression_shared_fixture_duplicate_registration_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    REQUIRED_SUBSTRING "registered multiple times with conflicting callbacks"
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_duplicate_registration_junit_reports_error
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    FILE ${CMAKE_BINARY_DIR}/regression_shared_fixture_duplicate_registration.xml
    REQUIRED_SUBSTRING "errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test --junit=${CMAKE_BINARY_DIR}/regression_shared_fixture_duplicate_registration.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_duplicate_registration_summary_failed_count
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
    REQUIRED_SUBSTRING "Summary: passed 1/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 0."
    ARGS --run=regressions/shared_fixture_duplicate_registration/smoke --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_scope_conflict_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_scope_conflict>
    REQUIRED_SUBSTRING "registered with conflicting scopes."
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

set(_gentest_shared_fixture_blocked_reason_regressions
    "regression_shared_fixture_manual_create_throw|gentest_regression_shared_fixture_manual_create_throw_skip|regressions/shared_fixture_manual_create_throw_skip/member_case|manual-create-throw"
    "regression_shared_fixture_manual_create_skip|gentest_regression_shared_fixture_manual_create_skip|regressions/shared_fixture_manual_create_skip/member_case|manual-create-skip"
    "regression_shared_fixture_manual_create_assert|gentest_regression_shared_fixture_manual_create_assert_skip|regressions/shared_fixture_manual_create_assert_skip/member_case|manual-create-assert"
    "regression_shared_fixture_manual_create_expect|gentest_regression_shared_fixture_manual_create_expect_skip|regressions/shared_fixture_manual_create_expect_skip/member_case|manual-create-expect"
    "regression_shared_fixture_manual_setup_assert|gentest_regression_shared_fixture_manual_setup_assert_skip|regressions/shared_fixture_manual_setup_assert_skip/member_case|manual-setup-assert"
    "regression_shared_fixture_manual_setup_skip|gentest_regression_shared_fixture_manual_setup_skip|regressions/shared_fixture_manual_setup_skip/member_case|fixture setup failed: manual-setup-skip"
    "regression_shared_fixture_manual_setup_throw|gentest_regression_shared_fixture_manual_setup_throw_skip|regressions/shared_fixture_manual_setup_throw_skip/member_case|manual-setup-throw"
    "regression_shared_fixture_missing_factory|gentest_regression_shared_fixture_missing_factory_skip|regressions/shared_fixture_missing_factory_skip/member_case|missing factory")

foreach(_gentest_shared_fixture_blocked_reason_regression IN LISTS _gentest_shared_fixture_blocked_reason_regressions)
    string(REPLACE "|" ";" _gentest_shared_fixture_blocked_reason_fields "${_gentest_shared_fixture_blocked_reason_regression}")
    list(GET _gentest_shared_fixture_blocked_reason_fields 0 _gentest_shared_fixture_blocked_reason_name)
    list(GET _gentest_shared_fixture_blocked_reason_fields 1 _gentest_shared_fixture_blocked_reason_prog)
    list(GET _gentest_shared_fixture_blocked_reason_fields 2 _gentest_shared_fixture_blocked_reason_run)
    list(GET _gentest_shared_fixture_blocked_reason_fields 3 _gentest_shared_fixture_blocked_reason_substring)

    _gentest_add_shared_fixture_blocked_reason_regression(
        NAME_PREFIX ${_gentest_shared_fixture_blocked_reason_name}
        PROG ${_gentest_shared_fixture_blocked_reason_prog}
        RUN ${_gentest_shared_fixture_blocked_reason_run}
        REQUIRED_SUBSTRING ${_gentest_shared_fixture_blocked_reason_substring})
endforeach()
unset(_gentest_shared_fixture_blocked_reason_regressions)
unset(_gentest_shared_fixture_blocked_reason_regression)
unset(_gentest_shared_fixture_blocked_reason_fields)
unset(_gentest_shared_fixture_blocked_reason_name)
unset(_gentest_shared_fixture_blocked_reason_prog)
unset(_gentest_shared_fixture_blocked_reason_run)
unset(_gentest_shared_fixture_blocked_reason_substring)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_setup_assert_runs_teardown
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_assert_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_setup_assert_skip/member_case --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=manual-setup-assert-teardown-ran")

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_setup_skip_runs_teardown
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_setup_skip/member_case --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=manual-setup-skip-teardown-ran")

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_create_stale_bench_error_cleared
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_stale_bench_error>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_manual_create_stale_bench_error/member_case --kind=test)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_create_stale_bench_error_not_reported
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_stale_bench_error>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_create_stale_bench_error/member_case --kind=test
    DEFINES
        "EXPECT_RC=0"
        "FORBID_SUBSTRING=stale-bench-error-marker")

gentest_add_check_death(
    NAME regression_shared_fixture_manual_create_expect_throw_reports_throw
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_expect_throw>
    REQUIRED_SUBSTRING "manual-create-throw-after-expect"
    ARGS --run=regressions/shared_fixture_manual_create_expect_throw/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_setup_expect_throw_reports_throw
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_expect_throw>
    REQUIRED_SUBSTRING "manual-setup-throw-after-expect"
    ARGS --run=regressions/shared_fixture_manual_setup_expect_throw/member_case --kind=test)

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_create_expect_skip_precedence_blocked
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_expect_skip_precedence>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 1
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_expect_skip_precedence/member_case --kind=test)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_create_expect_skip_precedence_reports_failure_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_expect_skip_precedence>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_create_expect_skip_precedence/member_case --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=manual-create-expect-before-skip"
        "FORBID_SUBSTRING=manual-create-skip-after-failure")

gentest_add_check_counts(
    NAME regression_shared_fixture_manual_setup_expect_skip_precedence_blocked
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_expect_skip_precedence>
    PASS 0
    FAIL 0
    SKIP 0
    BLOCKED 1
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_expect_skip_precedence/member_case --kind=test)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_setup_expect_skip_precedence_reports_failure_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_expect_skip_precedence>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_setup_expect_skip_precedence/member_case --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=manual-setup-expect-before-skip"
        "FORBID_SUBSTRING=manual-setup-skip-after-failure")

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_create_assert_junit_reports_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_reason.xml
    REQUIRED_SUBSTRING "manual-create-assert"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_reason.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_create_assert_junit_reports_blocked_outcome
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_mixed_outcome.xml
    REQUIRED_SUBSTRING "failures=\"0\" skipped=\"1\" errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_mixed_outcome.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_create_assert_junit_reports_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_failure_tag.xml
    REQUIRED_SUBSTRING "<skipped message=\"blocked: fixture allocation failed: ASSERT_TRUE  failed at tests/regressions/shared_fixture_manual_create_assert_skip.cpp:9: manual-create-assert\"/>"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_failure_tag.xml)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_create_assert_reports_location
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_assert_skip>
    REQUIRED_SUBSTRING "failed at tests/regressions/shared_fixture_manual_create_assert_skip.cpp:9"
    ARGS --run=regressions/shared_fixture_manual_create_assert_skip/member_case --kind=test)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_create_assert_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_create_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_location.xml
    REQUIRED_SUBSTRING "tests/regressions/shared_fixture_manual_create_assert_skip.cpp:9"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_create_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_create_assert_location.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_setup_assert_junit_reports_reason
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_reason.xml
    REQUIRED_SUBSTRING "manual-setup-assert"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_reason.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_setup_assert_junit_reports_blocked_outcome
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_mixed_outcome.xml
    REQUIRED_SUBSTRING "failures=\"0\" skipped=\"1\" errors=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_mixed_outcome.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_setup_assert_junit_reports_blocked_element
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_failure_tag.xml
    REQUIRED_SUBSTRING "<skipped message=\"blocked: fixture setup failed: ASSERT_TRUE  failed at tests/regressions/shared_fixture_manual_setup_assert_skip.cpp:13: manual-setup-assert\"/>"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_failure_tag.xml)

gentest_add_run_and_check_file(
    NAME regression_shared_fixture_manual_setup_assert_junit_reports_location
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_assert_skip>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_location.xml
    REQUIRED_SUBSTRING "tests/regressions/shared_fixture_manual_setup_assert_skip.cpp:13"
    EXPECT_RC 1
    ARGS --run=regressions/shared_fixture_manual_setup_assert_skip/member_case --kind=test --junit=${CMAKE_CURRENT_BINARY_DIR}/shared_fixture_manual_setup_assert_location.xml)

gentest_add_cmake_script_test(
    NAME regression_shared_fixture_manual_setup_throw_runs_teardown
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_setup_throw_skip>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckNoSubstring.cmake"
    ARGS --run=regressions/shared_fixture_manual_setup_throw_skip/member_case --kind=test
    DEFINES
        "EXPECT_RC=1"
        "REQUIRED_SUBSTRING=manual-setup-teardown-ran")

gentest_add_check_counts(
    NAME regression_shared_fixture_runtime_registration_during_run_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_runtime_registration_during_run>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_runtime_registration_during_run/late_register --kind=test)

gentest_add_check_exit_code(
    NAME regression_shared_fixture_retry_after_failure
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_retry_after_failure>
    EXPECT_RC 0)

gentest_add_check_counts(
    NAME regression_runtime_case_snapshot_isolated
    PROG $<TARGET_FILE:gentest_regression_runtime_case_snapshot_isolated>
    PASS 2
    FAIL 0
    SKIP 0
    ARGS --filter=regressions/runtime_case_snapshot_isolated/* --kind=test)

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

gentest_add_check_counts(
    NAME regression_shared_fixture_global_scope_most_specific_lookup
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_global_scope_most_specific>
    PASS 1
    FAIL 0
    SKIP 0
    ARGS --run=regressions/shared_fixture_global_scope_most_specific/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_global_scope_prefix_collision_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_global_scope_prefix_collision>
    REQUIRED_SUBSTRING "fixture not registered"
    ARGS --run=regressions/shared_fixture_global_scope_prefix_collision/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_suite_scope_prefix_collision_rejected
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_suite_scope_prefix_collision>
    REQUIRED_SUBSTRING "fixture not registered"
    ARGS --run=regressions/shared_fixture_suite_scope_prefix_collision/member_case --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_teardown_throw_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_teardown_throw_exit>
    REQUIRED_SUBSTRING "manual-teardown-throw"
    ARGS --run=regressions/shared_fixture_manual_teardown_throw_exit/smoke --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_teardown_assert_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_teardown_assert_exit>
    REQUIRED_SUBSTRING "manual-teardown-assert"
    ARGS --run=regressions/shared_fixture_manual_teardown_assert_exit/smoke --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_teardown_expect_throw_reports_throw
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_teardown_expect_throw_exit>
    REQUIRED_SUBSTRING "manual-teardown-throw-after-expect"
    ARGS --run=regressions/shared_fixture_manual_teardown_expect_throw_exit/smoke --kind=test)

gentest_add_check_death(
    NAME regression_shared_fixture_manual_teardown_skip_exits_nonzero
    PROG $<TARGET_FILE:gentest_regression_shared_fixture_manual_teardown_skip_exit>
    REQUIRED_SUBSTRING "manual-teardown-skip"
    ARGS --run=regressions/shared_fixture_manual_teardown_skip_exit/smoke --kind=test)

gentest_add_cmake_script_test(
    NAME regression_time_unit_tables
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTimeUnitTables.cmake")

gentest_add_cmake_script_test(
    NAME regression_time_unit_ns_override
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    SCRIPT "${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckTimeUnitNsOverride.cmake")

gentest_add_check_death(
    NAME regression_time_unit_invalid_value
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    REQUIRED_SUBSTRING "error: --time-unit must be one of auto,ns"
    ARGS --time-unit=not-a-unit)

gentest_add_check_death(
    NAME regression_time_unit_duplicate_value
    PROG $<TARGET_FILE:gentest_regression_time_unit_scaling>
    REQUIRED_SUBSTRING "error: duplicate --time-unit"
    ARGS --time-unit=auto --time-unit=ns)
