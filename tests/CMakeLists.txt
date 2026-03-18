include(GentestTests)
include("${PROJECT_SOURCE_DIR}/cmake/CheckModuleFixtureCommon.cmake")

if(NOT TARGET gentest_test_suites)
    add_custom_target(gentest_test_suites)
endif()

gentest_resolve_test_std(_gentest_test_std_feature _gentest_codegen_std_flag)
unset(_gentest_test_std_feature)

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
set(_gentest_codegen_parallel_bench_suites
    ${_gentest_default_suites}
    concurrency
    outcomes)
if(GENTEST_WITH_LIBASSERT)
    list(APPEND _gentest_codegen_parallel_bench_suites libassert)
endif()

set(_gentest_codegen_parallel_bench_sources "")
foreach(suite IN LISTS _gentest_codegen_parallel_bench_suites)
    list(APPEND _gentest_codegen_parallel_bench_sources ${CMAKE_CURRENT_SOURCE_DIR}/${suite}/cases.cpp)
endforeach()

add_library(gentest_codegen_parallel_bench_obj OBJECT EXCLUDE_FROM_ALL ${_gentest_codegen_parallel_bench_sources})

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
if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
    set_tests_properties(concurrency_fail_single_death concurrency_multi_noadopt_death PROPERTIES DISABLED "$<CONFIG:Debug>")
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
gentest_add_check_exit_code(
    NAME gentest_codegen_check_valid
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/cases.cpp
        -- ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_check_namespaced_attrs
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        -- ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_keyword
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "attribute 'death' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_value_keyword
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "attribute 'req(\"REQ-MIS-SCOPED\")' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mis_scoped_attr_unqualified_test
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "attribute 'test(\"smoke/mis_scoped/plain\")' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/mis_scoped_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_mixed_std_first
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_std_first.gentest.cpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_std_first.gentest.cpp"
        "EXPECT_SUBSTRING=smoke/namespaced/mixed/std_first")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_mixed_scoped_first
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_scoped_first.gentest.cpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_mixed_scoped_first.gentest.cpp"
        "EXPECT_SUBSTRING=smoke/namespaced/mixed/scoped_first")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_gentest_first_foreign_after
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_foreign_after.gentest.cpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_foreign_after.gentest.cpp"
        "EXPECT_SUBSTRING=smoke/namespaced/mixed/gentest_first_foreign_after")

gentest_add_cmake_script_test(
    NAME gentest_codegen_emit_namespaced_attrs_gentest_first_unscoped_foreign
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_unscoped_foreign.gentest.cpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespaced_attrs.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespaced_attrs_gentest_first_unscoped_foreign.gentest.cpp"
        "EXPECT_SUBSTRING=smoke/namespaced/mixed/gentest_first_unscoped_foreign")

gentest_add_cmake_script_test(
    NAME gentest_codegen_namespace_suite_attr_with_trailing_comment
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContains.cmake"
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/namespace_suite_comment.gentest.cpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/namespace_suite_comment.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        "FILE=${CMAKE_CURRENT_BINARY_DIR}/namespace_suite_comment.gentest.cpp"
        "EXPECT_SUBSTRING=suite_override/comment_backscan/trailing_comment_suite_attr_case")

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_unknown_attr
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "unknown gentest attribute 'unknown' (will_fail)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_cross_target_arg}
           ${_gentest_codegen_std_flag}
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_check_invalid_duplicate_template_attr
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "duplicate 'template' attribute for the same parameter (duplicate_template_attr)"
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke/invalid_attrs.cpp
        -- ${_gentest_codegen_cross_target_arg}
           ${_gentest_codegen_std_flag}
           -I${PROJECT_SOURCE_DIR}/include
           -I${CMAKE_CURRENT_SOURCE_DIR})

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
    EXPECT_SUBSTRING "target definition must be in a header or header module"
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
    NAME gentest_codegen_mock_nonvirtual_defined_in_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "target definition must be in a header or header module"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_cpp.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_named_module_codegen_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/named_module_ok.cppm
        --
        ${_gentest_codegen_cross_target_arg}
        -x
        c++
        -DGENTEST_CODEGEN=1
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_header_main_tu_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/header_main_tu.hpp
        --
        ${_gentest_codegen_cross_target_arg}
        -x
        c++
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_named_module_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/named_module_ok.cppm
        --
        ${_gentest_codegen_cross_target_arg}
        -x
        c++
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_cu_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "target definition must be in a header or header module"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_cu.cu
        --
        ${_gentest_codegen_cross_target_arg}
        -x
        c++
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cpp_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "target definition must be in a header or header module"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cpp.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cppm_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "target definition must be in a header or header module"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cppm.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_death(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_cu_msg
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_SUBSTRING "target definition must be in a header or header module"
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_cu.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_noext_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_noext.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_nonvirtual_defined_in_included_mpp_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --check
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/nonvirtual_defined_in_included_mpp.cpp
        --
        ${_gentest_codegen_cross_target_arg}
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

gentest_add_check_exit_code(
    NAME gentest_codegen_mock_emit_header_main_tu_ok
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    EXPECT_RC 0
    ARGS
        --output
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu.gentest.cpp
        --mock-registry
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_registry.hpp
        --mock-impl
        ${CMAKE_CURRENT_BINARY_DIR}/mock_emit_header_main_tu_mock_impl.hpp
        --compdb
        ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_errors/header_main_tu.hpp
        --
        ${_gentest_codegen_cross_target_arg}
        -x
        c++
        ${_gentest_codegen_std_flag}
        -I${PROJECT_SOURCE_DIR}/include
        -I${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    gentest_add_cmake_script_test(
        NAME gentest_codegen_mock_cross_root_include_windows
        NO_EMULATOR
        PROG ${_gentest_codegen_prog}
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckMockCrossRootInclude.cmake"
        DEFINES
            "BUILD_ROOT=${CMAKE_BINARY_DIR}"
            "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "TESTS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "CODEGEN_STD=${_gentest_codegen_std_flag}"
            "CROSS_TARGET_ARG=${_gentest_codegen_cross_target_arg}")
endif()

gentest_add_cmake_script_test(
    NAME gentest_codegen_mock_symlink_include
    NO_EMULATOR
    PROG ${_gentest_codegen_prog}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckMockSymlinkInclude.cmake"
    DEFINES
        "BUILD_ROOT=${CMAKE_BINARY_DIR}"
        "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "CODEGEN_STD=${_gentest_codegen_std_flag}"
        "CROSS_TARGET_ARG=${_gentest_codegen_cross_target_arg}")

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
    gentest_codegen_check_namespaced_attrs
    gentest_codegen_mis_scoped_attr_keyword
    gentest_codegen_mis_scoped_attr_value_keyword
    gentest_codegen_mis_scoped_attr_unqualified_test
    gentest_codegen_namespace_suite_attr_with_trailing_comment
    gentest_codegen_check_invalid_unknown_attr
    gentest_codegen_check_invalid_duplicate_template_attr
    gentest_codegen_baseline_requires_bench
    gentest_codegen_mock_incomplete_virtual_msg
    gentest_codegen_mock_virtual_defined_in_cpp_msg
    gentest_codegen_mock_nonvirtual_defined_in_cpp_msg
    gentest_codegen_mock_named_module_codegen_ok
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
    gentest_codegen_fixture_attr_removed
    PROPERTIES LABELS "codegen")

if(WIN32)
    set_property(TEST gentest_codegen_mock_cross_root_include_windows APPEND PROPERTY LABELS "codegen")
endif()

# End-to-end behavioral checks with structured suite inventory
set(_gentest_suite_inventory_checks
    "unit|gentest_unit_tests|20|20|0|0|${CMAKE_CURRENT_SOURCE_DIR}/expected/unit.list"
    "integration|gentest_integration_tests|4|3|0|1|${CMAKE_CURRENT_SOURCE_DIR}/expected/integration.list"
    "skiponly|gentest_skiponly_tests|2|0|0|2|${CMAKE_CURRENT_SOURCE_DIR}/expected/skiponly.list"
    "fixtures|gentest_fixtures_tests|35|35|0|0|${CMAKE_CURRENT_SOURCE_DIR}/expected/fixtures.list"
    "readme_fixtures|gentest_readme_fixtures_tests|3|3|0|0|${CMAKE_CURRENT_SOURCE_DIR}/expected/readme_fixtures.list"
    "templates|gentest_templates_tests|93|93|0|0|${CMAKE_CURRENT_SOURCE_DIR}/expected/templates.list"
    "mocking|gentest_mocking_tests|28|28|0|0|${CMAKE_CURRENT_SOURCE_DIR}/expected/mocking.list")

foreach(_gentest_inventory IN LISTS _gentest_suite_inventory_checks)
    string(REPLACE "|" ";" _gentest_inventory_fields "${_gentest_inventory}")
    list(GET _gentest_inventory_fields 0 _gentest_inventory_name)
    list(GET _gentest_inventory_fields 1 _gentest_inventory_prog)
    list(GET _gentest_inventory_fields 2 _gentest_inventory_cases)
    list(GET _gentest_inventory_fields 3 _gentest_inventory_pass)
    list(GET _gentest_inventory_fields 4 _gentest_inventory_fail)
    list(GET _gentest_inventory_fields 5 _gentest_inventory_skip)
    list(GET _gentest_inventory_fields 6 _gentest_inventory_expected_list)

    gentest_add_check_inventory(
        NAME ${_gentest_inventory_name}_inventory
        PROG ${_gentest_inventory_prog}
        CASES ${_gentest_inventory_cases}
        EXPECTED_LIST_FILE ${_gentest_inventory_expected_list}
        PASS ${_gentest_inventory_pass}
        FAIL ${_gentest_inventory_fail}
        SKIP ${_gentest_inventory_skip})
endforeach()
unset(_gentest_suite_inventory_checks)
unset(_gentest_inventory)
unset(_gentest_inventory_fields)
unset(_gentest_inventory_name)
unset(_gentest_inventory_prog)
unset(_gentest_inventory_cases)
unset(_gentest_inventory_pass)
unset(_gentest_inventory_fail)
unset(_gentest_inventory_skip)
unset(_gentest_inventory_expected_list)

gentest_add_check_counts(NAME failing_counts PROG $<TARGET_FILE:gentest_failing_tests> PASS 0 FAIL 6 SKIP 2)

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
    EXPECT_SUBSTRING "fixture 'failing::NullSuiteFixture' fixture allocation returned null"
    ARGS --run=failing/alloc/suite_null)

gentest_add_check_death(
    NAME failing_global_fixture_alloc_message
    PROG $<TARGET_FILE:gentest_failing_tests>
    EXPECT_SUBSTRING "fixture 'failing::NullGlobalFixture' fixture allocation returned null"
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
    EXPECT_SUBSTRING "failures=\"0\""
    ARGS --run=outcomes/runtime_skip_prefix_user_text --junit=${CMAKE_BINARY_DIR}/outcomes_junit_runtime_skip_prefix.xml)

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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Regressions.cmake)

# CLI checks for VSCode integration
gentest_add_check_contains(NAME unit_help PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING gentest ARGS --help)
gentest_add_check_contains(NAME unit_help_time_unit PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING "--time-unit=<mode>" ARGS --help)
gentest_add_check_contains(NAME unit_list_contains_attr_close_marker_name_prefix PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING "unit/attributes/close_marker_in_string_" ARGS --list-tests)
gentest_add_check_contains(NAME unit_list_tags_fast PROG $<TARGET_FILE:gentest_unit_tests> EXPECT_SUBSTRING "tags=fast" ARGS --list)
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

gentest_add_check_counts(NAME repeat_unit_twice PROG $<TARGET_FILE:gentest_unit_tests> PASS 40 FAIL 0 SKIP 0 ARGS --repeat=2)

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
        EXPECT_SUBSTRING "\"status\":\"passed\""
        ARGS
            --run=unit/arithmetic/sum
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_unit_single)
    set_tests_properties(allure_smoke_single PROPERTIES LABELS "allure")

    # Allure logs attachment: failing test with log_on_fail(true) should create a logs attachment
    gentest_add_run_and_check_file(
        NAME allure_logs_attachment
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_json/result-0-result.json
        EXPECT_SUBSTRING "result-0-attachment.txt"
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_json)
    set_tests_properties(allure_logs_attachment PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_has_plain_text_type
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_typed/result-0-result.json
        EXPECT_SUBSTRING "\"name\":\"logs\",\"source\":\"result-0-attachment.txt\",\"type\":\"text/plain\""
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_typed)
    set_tests_properties(allure_logs_attachment_has_plain_text_type PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_logs_attachment_file
        PROG $<TARGET_FILE:gentest_failing_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_logs_file/result-0-attachment.txt
        EXPECT_SUBSTRING "hello from log"
        EXPECT_RC 1
        ARGS
            --run=failing/logging/attachment
            --allure-dir=${CMAKE_CURRENT_BINARY_DIR}/allure_logs_file)
    set_tests_properties(allure_logs_attachment_file PROPERTIES LABELS "allure")

    gentest_add_run_and_check_file(
        NAME allure_bench_metrics_attachment_json
        PROG $<TARGET_FILE:gentest_benchmarks_tests>
        FILE ${CMAKE_CURRENT_BINARY_DIR}/allure_bench_metrics_json/result-0-result.json
        EXPECT_SUBSTRING "\"name\":\"metrics\",\"source\":\"result-0-metrics.tsv\",\"type\":\"text/tab-separated-values\""
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
        EXPECT_SUBSTRING "best_ns_per_op"
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
        EXPECT_SUBSTRING "\"name\":\"summary-plot\",\"source\":\"result-0-summary-plot.svg\",\"type\":\"image/svg+xml\""
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
        EXPECT_SUBSTRING "<svg"
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
        EXPECT_SUBSTRING "\"name\":\"histogram\",\"source\":\"result-0-histogram.tsv\",\"type\":\"text/tab-separated-values\""
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
        EXPECT_SUBSTRING "range_lo_ns"
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
        EXPECT_SUBSTRING "\"name\":\"histogram-plot\",\"source\":\"result-0-histogram-plot.svg\",\"type\":\"image/svg+xml\""
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
        EXPECT_SUBSTRING "<svg"
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
        EXPECT_SUBSTRING "\"name\":\"samples\",\"source\":\"result-0-samples.json\",\"type\":\"application/json\""
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
        EXPECT_SUBSTRING "\"stored_count\":"
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

    gentest_add_cmake_script_test(
        NAME regression_allure_directory_failure_reports_infra
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckAllureDirFailure.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/allure_dir_failure/output.json"
            "EXPECT_SUBSTRING=failed to prepare Allure report directory:")
    set_tests_properties(regression_allure_directory_failure_reports_infra PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_allure_result_write_failure_reports_infra
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckAllureResultWriteFailure.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_result_write_failure"
            "EXPECT_SUBSTRING=failed to open Allure result:")
    set_tests_properties(regression_allure_result_write_failure_reports_infra PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_allure_result_write_failure_keeps_junit_in_sync
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckAllureJunitParity.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allure_junit_parity"
            "JUNIT_PATH=${CMAKE_CURRENT_BINARY_DIR}/allure_junit_parity/result.xml"
            "EXPECT_SUBSTRING=failed to open Allure result:"
            "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
            "EXPECT_JUNIT_COUNTS=tests=\"1\" failures=\"0\" skipped=\"0\" errors=\"1\"")
    set_tests_properties(regression_allure_result_write_failure_keeps_junit_in_sync PROPERTIES LABELS "allure")

    gentest_add_cmake_script_test(
        NAME regression_junit_open_failure_keeps_allure_in_sync
        PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckJunitAllureParity.cmake"
        ARGS
            --run=regressions/runtime_reporting/pass_for_junit_io_visibility
            --kind=test
        DEFINES
            "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_parity"
            "JUNIT_PATH=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_parity/junit-as-directory"
            "EXPECT_SUBSTRING=failed to open JUnit report:"
            "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
            "EXPECT_RESULT_COUNT=2")
    set_tests_properties(regression_junit_open_failure_keeps_allure_in_sync PROPERTIES LABELS "allure")

    if(UNIX AND EXISTS "/dev/full")
        gentest_add_cmake_script_test(
            NAME regression_junit_write_failure_keeps_allure_in_sync
            PROG $<TARGET_FILE:gentest_regression_runtime_reporting>
            SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckJunitAllureParity.cmake"
            ARGS
                --run=regressions/runtime_reporting/pass_for_junit_io_visibility
                --kind=test
            DEFINES
                "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/junit_allure_write_parity"
                "JUNIT_PATH=/dev/full"
                "PREPARE_JUNIT_AS_DIRECTORY=OFF"
                "EXPECT_SUBSTRING=failed to write JUnit report:"
                "EXPECT_CASE_NAME=regressions/runtime_reporting/pass_for_junit_io_visibility"
                "EXPECT_RESULT_COUNT=2")
        set_tests_properties(regression_junit_write_failure_keeps_allure_in_sync PROPERTIES LABELS "allure")
    endif()

    gentest_add_cmake_script_test(
        NAME regression_allure_runner_infra_failure_parity
        PROG $<TARGET_FILE:gentest_regression_shared_fixture_duplicate_registration>
        SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckAllureRunnerInfraParity.cmake"
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
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckFileContainsRejectsStale.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake_stale_file_contains")

gentest_add_cmake_script_test(
    NAME helper_allure_runner_infra_parity_rejects_stale_output
    PROG ${CMAKE_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckAllureRunnerInfraParityRejectsStale.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake_stale_allure")

gentest_add_cmake_script_test(
    NAME gentest_cdash_experimental_dry_run
    PROG ${CMAKE_CTEST_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckCdashExperimentalDryRun.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cmake_cdash_dry_run")

gentest_add_cmake_script_test(
    NAME gentest_cdash_experimental_live_smoke
    PROG ${CMAKE_CTEST_COMMAND}
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckCdashExperimentalLive.cmake"
    DEFINES
        "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cmake_cdash_live")

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
    EXPECT_SUBSTRING "shared fixture unavailable for 'benchmarks::NullBenchFixture'"
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
    EXPECT_SUBSTRING "shared fixture unavailable for 'benchmarks::NullJitterFixture'"
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

gentest_add_run_and_check_file(
    NAME regression_bench_only_junit_generation
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_only_junit.xml
    EXPECT_SUBSTRING "tests=\"1\""
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_only_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_only_junit_generation
    PROG $<TARGET_FILE:gentest_benchmarks_tests>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_only_junit.xml
    EXPECT_SUBSTRING "tests=\"1\""
    EXPECT_RC 0
    ARGS --run=benchmarks/fixture/free_suite_global_jitter --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_only_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_bench_failure_junit_generation
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/bench_failure_junit.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/bench_assert_should_fail --kind=bench --junit=${CMAKE_CURRENT_BINARY_DIR}/bench_failure_junit.xml)

gentest_add_run_and_check_file(
    NAME regression_jitter_failure_junit_generation
    PROG $<TARGET_FILE:gentest_regression_bench_assert>
    FILE ${CMAKE_CURRENT_BINARY_DIR}/jitter_failure_junit.xml
    EXPECT_SUBSTRING "failures=\"1\""
    EXPECT_RC 1
    ARGS --run=regressions/jitter_assert_should_fail --kind=jitter --junit=${CMAKE_CURRENT_BINARY_DIR}/jitter_failure_junit.xml)

gentest_add_cmake_script_test(
    NAME regression_outcome_consistency_matrix
    PROG $<TARGET_FILE:gentest_unit_tests>
    SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CheckOutcomeConsistencyMatrix.cmake"
    DEFINES
        "PROG_UNIT=$<TARGET_FILE:gentest_unit_tests>"
        "PROG_OUTCOMES=$<TARGET_FILE:gentest_outcomes_tests>"
        "PROG_INFRA_TEST=$<TARGET_FILE:gentest_regression_shared_fixture_setup_skip>"
        "PROG_INFRA_MEASURED=$<TARGET_FILE:gentest_regression_member_shared_fixture_setup_skip_bench_jitter>"
        "OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/outcome_consistency_matrix")

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
    NAME regression_cli_kind_mismatch_diagnostics
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "Case 'unit/arithmetic/sum' does not match --kind=bench"
    ARGS --run=unit/arithmetic/sum --kind=bench)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_diagnostics
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    EXPECT_SUBSTRING "Case name is ambiguous: common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_lists_alpha
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    EXPECT_SUBSTRING "regressions/ambiguity/alpha/common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME regression_cli_suffix_ambiguity_lists_beta
    PROG $<TARGET_FILE:gentest_regression_cli_suffix_ambiguity>
    EXPECT_SUBSTRING "regressions/ambiguity/beta/common_leaf"
    ARGS --run=common_leaf --kind=test)

gentest_add_check_death(
    NAME cli_bench_table_invalid_kind
    PROG $<TARGET_FILE:gentest_unit_tests>
    EXPECT_SUBSTRING "error: --bench-table requires --kind=bench or --kind=all"
    ARGS --bench-table --kind=jitter)

function(_gentest_add_cmake_helper_test name source_dir script_path label)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${source_dir}
            -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/${name}
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
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            ${ARGN}
            -P ${script_path})

    set_tests_properties(${name} PROPERTIES LABELS "${label}")
    set_property(TEST ${name} APPEND PROPERTY SKIP_REGULAR_EXPRESSION "GENTEST_SKIP_TEST:")
endfunction()

_gentest_add_cmake_helper_test(
    gentest_codegen_output_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_collision
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenOutputCollision.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_codegen_genex_sources_rejected
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_genex_sources
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenOutputCollision.cmake
    cmake
    -DEXPECT_SUBSTRING=generator-expression SOURCES entries are not supported)
set_property(TEST gentest_codegen_genex_sources_rejected APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_output_genex_rejected
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_output_genex
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenOutputCollision.cmake
    cmake
    -DEXPECT_SUBSTRING=OUTPUT with generator expressions is not supported in manifest mode)
set_property(TEST gentest_codegen_output_genex_rejected APPEND PROPERTY LABELS "codegen")

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

_gentest_add_cmake_helper_test(
    gentest_tu_header_case_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_header_case_collision
    ${PROJECT_SOURCE_DIR}/cmake/CheckTuHeaderCaseCollision.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_tu_header_case_collision APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_tu_register_symbol_collision
    ${PROJECT_SOURCE_DIR}/tests/cmake/tu_register_symbol_collision
    ${PROJECT_SOURCE_DIR}/cmake/CheckTuRegisterSymbolCollision.cmake
    cmake
    -DPROG=${_gentest_codegen_prog}
    -DTARGET_ARG=${_gentest_codegen_cross_target_arg})
set_property(TEST gentest_tu_register_symbol_collision APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_resource_dir_from_compdb_compiler
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenResourceDirFromCompdbCompiler.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_resource_dir_from_compdb_compiler APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_incremental_dependencies
    ${PROJECT_SOURCE_DIR}/tests/cmake/codegen_incremental_deps
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenIncrementalDependencies.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_codegen_incremental_dependencies APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_manifest_depfile_aggregation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenManifestDepfileAggregation.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_manifest_depfile_aggregation APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_env_wrapped_compiler
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenEnvWrappedCompiler.cmake
    cmake
    -DPROG=${_gentest_codegen_prog})
set_property(TEST gentest_codegen_env_wrapped_compiler APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_codegen_module_command_retargeting
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCodegenModuleCommandRetargeting.cmake
    cmake)
set_property(TEST gentest_codegen_module_command_retargeting APPEND PROPERTY LABELS "codegen")

_gentest_add_cmake_helper_test(
    gentest_public_mock_module_gcc
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckPublicMockModuleGccFailure.cmake
    cmake)
set_property(TEST gentest_public_mock_module_gcc APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_mixed_module_mock_registry
    ${PROJECT_SOURCE_DIR}/tests/cmake/mixed_module_mock_registry
    ${PROJECT_SOURCE_DIR}/cmake/CheckMixedModuleMockRegistry.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_mixed_module_mock_registry APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_mock_additive_visibility
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_additive_visibility
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleMockAdditiveVisibility.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_additive_visibility APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_mock_imported_sibling
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_mock_imported_sibling
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleMockImportedSibling.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_mock_imported_sibling APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_pcm_cache_isolation
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_pcm_cache_isolation
    ${PROJECT_SOURCE_DIR}/cmake/CheckModulePcmCacheIsolation.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_pcm_cache_isolation APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_auto_discovery
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_autodiscovery
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleAutoDiscovery.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_auto_discovery APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_multiline_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_multiline_directives
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleMultilineDirectiveFailure.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_multiline_directives APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_same_line_directives
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_same_line_directives
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleSameLineDirectives.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_same_line_directives APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_cpp_source
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_cpp_source
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleCppSource.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_cpp_source APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_first_classic_order
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_first_classic_order
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleFirstClassicOrder.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_first_classic_order APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_manual_include_whitespace
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_manual_include_whitespace
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleManualIncludeWhitespaceDuplicate.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_manual_include_whitespace APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_name_literal_false_match
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleNameLiteralFalseMatch.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_name_literal_false_match APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_header_unit_import_preamble
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_header_unit_import_preamble
    ${PROJECT_SOURCE_DIR}/cmake/CheckModuleHeaderUnitImportPreamble.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_header_unit_import_preamble APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_module_partition_import_shorthand
    ${PROJECT_SOURCE_DIR}/tests/cmake/module_partition_import_shorthand
    ${PROJECT_SOURCE_DIR}/cmake/CheckModulePartitionImportShorthand.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_module_partition_import_shorthand APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_old_ninja_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/public_module_support_fallback
    ${PROJECT_SOURCE_DIR}/cmake/CheckPublicModuleSupportFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DFALLBACK_MODE=old_ninja)
set_property(TEST gentest_public_module_old_ninja_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_missing_scan_deps_fallback
    ${PROJECT_SOURCE_DIR}/tests/cmake/public_module_support_fallback
    ${PROJECT_SOURCE_DIR}/cmake/CheckPublicModuleSupportFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DFALLBACK_MODE=missing_scan_deps)
set_property(TEST gentest_public_module_missing_scan_deps_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_public_module_gcc_version_fallback
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckPublicModuleGccVersionFallback.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_public_module_gcc_version_fallback APPEND PROPERTY LABELS "modules")

_gentest_add_cmake_helper_test(
    gentest_outcome_matrix_stale_junit_guard
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckOutcomeConsistencyMatrixStaleJunit.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_windows_debug_death_skip_gate
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckWindowsDebugDeathSkipGate.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_cross_compile_emulator_serialization
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCrossCompileEmulatorSerialization.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_cross_compile_exit_code_no_emulator
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCrossCompileExitCodeNoEmulator.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_missing_host_codegen_path
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckMissingHostCodegenPath.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_coverage_workflow_serial_ctest
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckCoverageWorkflowSerialCtest.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_qemu_workflow_presets
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckQemuWorkflowPresets.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_package_workflow_preset
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckPackageWorkflowPreset.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_vcpkg_manifest_metadata
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckVcpkgManifestMetadata.cmake
    cmake)

_gentest_add_cmake_helper_test(
    gentest_package_consumer_workdir_isolation
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumerWorkdirIsolation.cmake
    package
    -DPACKAGE_NAME=${PROJECT_NAME})
set_property(TEST gentest_package_consumer_workdir_isolation APPEND PROPERTY LABELS "modules")

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
        gentest_is_clang_like(_gentest_package_test_top_level_is_clang "${CMAKE_CXX_COMPILER}")
        if(NOT _gentest_package_test_top_level_is_clang)
            gentest_resolve_clang_fixture_compilers(_gentest_package_test_module_c _gentest_package_test_module_cxx)
            if(NOT "${_gentest_package_test_module_c}" STREQUAL "" AND NOT "${_gentest_package_test_module_cxx}" STREQUAL "")
                set(_gentest_package_test_module_compiler_args
                    -DPACKAGE_TEST_C_COMPILER=${_gentest_package_test_module_c}
                    -DPACKAGE_TEST_CXX_COMPILER=${_gentest_package_test_module_cxx})
                gentest_find_clang_scan_deps(_gentest_package_test_module_scan_deps "${_gentest_package_test_module_cxx}")
                if(NOT "${_gentest_package_test_module_scan_deps}" STREQUAL "")
                    list(APPEND _gentest_package_test_module_compiler_args
                        -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${_gentest_package_test_module_scan_deps})
                endif()
            endif()
        endif()
    endif()

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_include_only
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=main_only
        -DPACKAGE_TEST_USE_MODULES=OFF
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_compiler_args})

    _gentest_add_cmake_helper_test(
        gentest_package_consumer_include_only_native_codegen
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
        ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
        package
        -DPACKAGE_NAME=${PROJECT_NAME}
        -DBUILD_CONFIG=${_gentest_package_test_config}
        -DCONSUMER_LINK_MODE=double
        -DPACKAGE_TEST_USE_MODULES=ON
        -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON
        ${_gentest_package_test_module_compiler_args})
    set_property(TEST gentest_package_consumer_double APPEND PROPERTY LABELS "modules")

    if(UNIX AND NOT APPLE)
        find_program(_gentest_package_test_gcc NAMES gcc)
        find_program(_gentest_package_test_gxx NAMES g++)
        if(_gentest_package_test_gcc AND _gentest_package_test_gxx)
            _gentest_add_cmake_helper_test(
                gentest_package_consumer_gcc
                ${PROJECT_SOURCE_DIR}
                ${PROJECT_SOURCE_DIR}/cmake/CheckPackageConsumer.cmake
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
