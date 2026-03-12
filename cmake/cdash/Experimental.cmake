cmake_minimum_required(VERSION 3.31)

get_filename_component(_gentest_cdash_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(CTEST_SOURCE_DIRECTORY "${_gentest_cdash_repo_root}")
if(DEFINED ENV{GENTEST_CDASH_SOURCE_DIR} AND NOT "$ENV{GENTEST_CDASH_SOURCE_DIR}" STREQUAL "")
    set(CTEST_SOURCE_DIRECTORY "$ENV{GENTEST_CDASH_SOURCE_DIR}")
endif()

set(CTEST_BINARY_DIRECTORY "${CTEST_SOURCE_DIRECTORY}/build/cdash-experimental")
if(DEFINED ENV{GENTEST_CDASH_BINARY_DIR} AND NOT "$ENV{GENTEST_CDASH_BINARY_DIR}" STREQUAL "")
    set(CTEST_BINARY_DIRECTORY "$ENV{GENTEST_CDASH_BINARY_DIR}")
endif()

set(_gentest_cdash_model "Experimental")
if(DEFINED ENV{GENTEST_CDASH_MODEL} AND NOT "$ENV{GENTEST_CDASH_MODEL}" STREQUAL "")
    set(_gentest_cdash_model "$ENV{GENTEST_CDASH_MODEL}")
endif()

set(_gentest_cdash_group "${_gentest_cdash_model}")
if(DEFINED ENV{GENTEST_CDASH_GROUP} AND NOT "$ENV{GENTEST_CDASH_GROUP}" STREQUAL "")
    set(_gentest_cdash_group "$ENV{GENTEST_CDASH_GROUP}")
endif()

set(CTEST_CMAKE_GENERATOR "Ninja")
if(DEFINED ENV{GENTEST_CDASH_GENERATOR} AND NOT "$ENV{GENTEST_CDASH_GENERATOR}" STREQUAL "")
    set(CTEST_CMAKE_GENERATOR "$ENV{GENTEST_CDASH_GENERATOR}")
endif()

set(CTEST_BUILD_CONFIGURATION "Debug")
if(DEFINED ENV{GENTEST_CDASH_CONFIGURATION} AND NOT "$ENV{GENTEST_CDASH_CONFIGURATION}" STREQUAL "")
    set(CTEST_BUILD_CONFIGURATION "$ENV{GENTEST_CDASH_CONFIGURATION}")
endif()

set(_gentest_cdash_parallel_level "8")
if(DEFINED ENV{GENTEST_CDASH_PARALLEL_LEVEL} AND NOT "$ENV{GENTEST_CDASH_PARALLEL_LEVEL}" STREQUAL "")
    set(_gentest_cdash_parallel_level "$ENV{GENTEST_CDASH_PARALLEL_LEVEL}")
endif()

set(_gentest_cdash_submit_url "")
if(DEFINED ENV{GENTEST_CDASH_SUBMIT_URL} AND NOT "$ENV{GENTEST_CDASH_SUBMIT_URL}" STREQUAL "")
    set(_gentest_cdash_submit_url "$ENV{GENTEST_CDASH_SUBMIT_URL}")
elseif(DEFINED CTEST_SUBMIT_URL AND NOT "${CTEST_SUBMIT_URL}" STREQUAL "")
    set(_gentest_cdash_submit_url "${CTEST_SUBMIT_URL}")
endif()

set(_gentest_cdash_enable_allure "OFF")
if(DEFINED ENV{GENTEST_CDASH_ENABLE_ALLURE_TESTS} AND NOT "$ENV{GENTEST_CDASH_ENABLE_ALLURE_TESTS}" STREQUAL "")
    set(_gentest_cdash_enable_allure "$ENV{GENTEST_CDASH_ENABLE_ALLURE_TESTS}")
endif()
string(TOUPPER "${_gentest_cdash_enable_allure}" _gentest_cdash_enable_allure_value)
if(_gentest_cdash_enable_allure_value STREQUAL "1" OR
   _gentest_cdash_enable_allure_value STREQUAL "ON" OR
   _gentest_cdash_enable_allure_value STREQUAL "TRUE" OR
   _gentest_cdash_enable_allure_value STREQUAL "YES")
    set(_gentest_cdash_enable_allure "ON")
else()
    set(_gentest_cdash_enable_allure "OFF")
endif()

set(_gentest_cdash_build_targets "")
if(DEFINED ENV{GENTEST_CDASH_BUILD_TARGETS} AND NOT "$ENV{GENTEST_CDASH_BUILD_TARGETS}" STREQUAL "")
    set(_gentest_cdash_build_targets "$ENV{GENTEST_CDASH_BUILD_TARGETS}")
    string(REPLACE "," ";" _gentest_cdash_build_targets "${_gentest_cdash_build_targets}")
endif()

set(_gentest_cdash_test_regex "")
if(DEFINED ENV{GENTEST_CDASH_TEST_REGEX} AND NOT "$ENV{GENTEST_CDASH_TEST_REGEX}" STREQUAL "")
    set(_gentest_cdash_test_regex "$ENV{GENTEST_CDASH_TEST_REGEX}")
endif()

set(_gentest_cdash_dry_run OFF)
if(DEFINED ENV{GENTEST_CDASH_DRY_RUN} AND NOT "$ENV{GENTEST_CDASH_DRY_RUN}" STREQUAL "")
    string(TOUPPER "$ENV{GENTEST_CDASH_DRY_RUN}" _gentest_cdash_dry_run_value)
    if(_gentest_cdash_dry_run_value STREQUAL "1" OR
       _gentest_cdash_dry_run_value STREQUAL "ON" OR
       _gentest_cdash_dry_run_value STREQUAL "TRUE" OR
       _gentest_cdash_dry_run_value STREQUAL "YES")
        set(_gentest_cdash_dry_run ON)
    endif()
endif()

if(NOT DEFINED CTEST_SITE OR "${CTEST_SITE}" STREQUAL "")
    site_name(CTEST_SITE)
endif()

if(NOT DEFINED CTEST_BUILD_NAME OR "${CTEST_BUILD_NAME}" STREQUAL "")
    set(CTEST_BUILD_NAME "${CTEST_SITE}-${_gentest_cdash_model}-${CTEST_BUILD_CONFIGURATION}-${CTEST_CMAKE_GENERATOR}")
endif()

set(CTEST_USE_LAUNCHERS 1)
file(MAKE_DIRECTORY "${CTEST_BINARY_DIRECTORY}")

set(_gentest_cdash_configure_options
    "-Dgentest_BUILD_TESTING=ON"
    "-DGENTEST_BUILD_CODEGEN=ON"
    "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF"
    "-DCMAKE_BUILD_TYPE=${CTEST_BUILD_CONFIGURATION}"
    "-DGENTEST_ENABLE_ALLURE_TESTS=${_gentest_cdash_enable_allure}")
if(_gentest_cdash_enable_allure STREQUAL "ON")
    list(APPEND _gentest_cdash_configure_options "-DGENTEST_USE_BOOST_JSON=ON")
endif()

if(DEFINED ENV{GENTEST_CDASH_CONFIGURE_OPTIONS} AND NOT "$ENV{GENTEST_CDASH_CONFIGURE_OPTIONS}" STREQUAL "")
    set(_gentest_cdash_extra_options "$ENV{GENTEST_CDASH_CONFIGURE_OPTIONS}")
    separate_arguments(_gentest_cdash_extra_options NATIVE_COMMAND "${_gentest_cdash_extra_options}")
    list(APPEND _gentest_cdash_configure_options ${_gentest_cdash_extra_options})
endif()

set(_gentest_cdash_build_targets_summary "${_gentest_cdash_build_targets}")
string(REPLACE ";" "," _gentest_cdash_build_targets_summary "${_gentest_cdash_build_targets_summary}")

string(JOIN "\n" _gentest_cdash_summary
       "source=${CTEST_SOURCE_DIRECTORY}"
       "binary=${CTEST_BINARY_DIRECTORY}"
       "model=${_gentest_cdash_model}"
       "group=${_gentest_cdash_group}"
       "generator=${CTEST_CMAKE_GENERATOR}"
       "configuration=${CTEST_BUILD_CONFIGURATION}"
       "parallel_level=${_gentest_cdash_parallel_level}"
       "submit_url=${_gentest_cdash_submit_url}"
       "allure_tests=${_gentest_cdash_enable_allure}"
       "build_targets=${_gentest_cdash_build_targets_summary}"
       "test_regex=${_gentest_cdash_test_regex}")

message(STATUS "gentest-cdash:\n${_gentest_cdash_summary}")

ctest_start("${_gentest_cdash_model}" "${CTEST_SOURCE_DIRECTORY}" "${CTEST_BINARY_DIRECTORY}" GROUP "${_gentest_cdash_group}")

if(_gentest_cdash_dry_run)
    file(WRITE "${CTEST_BINARY_DIRECTORY}/cdash-dry-run.txt" "${_gentest_cdash_summary}\n")
    return()
endif()

ctest_configure(
    BUILD "${CTEST_BINARY_DIRECTORY}"
    SOURCE "${CTEST_SOURCE_DIRECTORY}"
    OPTIONS "${_gentest_cdash_configure_options}"
    RETURN_VALUE _gentest_cdash_configure_rc
    CAPTURE_CMAKE_ERROR _gentest_cdash_configure_err)
if(_gentest_cdash_configure_err LESS 0 OR NOT _gentest_cdash_configure_rc EQUAL 0)
    message(FATAL_ERROR "gentest-cdash: configure failed (rc=${_gentest_cdash_configure_rc}, cmake_error=${_gentest_cdash_configure_err})")
endif()

if(_gentest_cdash_build_targets STREQUAL "")
    ctest_build(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        CONFIGURATION "${CTEST_BUILD_CONFIGURATION}"
        PARALLEL_LEVEL "${_gentest_cdash_parallel_level}"
        NUMBER_ERRORS _gentest_cdash_build_errors
        NUMBER_WARNINGS _gentest_cdash_build_warnings
        RETURN_VALUE _gentest_cdash_build_rc
        CAPTURE_CMAKE_ERROR _gentest_cdash_build_err)
    if(_gentest_cdash_build_err LESS 0 OR NOT _gentest_cdash_build_rc EQUAL 0 OR NOT _gentest_cdash_build_errors EQUAL 0)
        message(FATAL_ERROR
            "gentest-cdash: build failed (rc=${_gentest_cdash_build_rc}, errors=${_gentest_cdash_build_errors}, warnings=${_gentest_cdash_build_warnings}, cmake_error=${_gentest_cdash_build_err})")
    endif()
else()
    foreach(_gentest_cdash_build_target IN LISTS _gentest_cdash_build_targets)
        ctest_build(
            BUILD "${CTEST_BINARY_DIRECTORY}"
            CONFIGURATION "${CTEST_BUILD_CONFIGURATION}"
            PARALLEL_LEVEL "${_gentest_cdash_parallel_level}"
            TARGET "${_gentest_cdash_build_target}"
            NUMBER_ERRORS _gentest_cdash_build_errors
            NUMBER_WARNINGS _gentest_cdash_build_warnings
            RETURN_VALUE _gentest_cdash_build_rc
            CAPTURE_CMAKE_ERROR _gentest_cdash_build_err)
        if(_gentest_cdash_build_err LESS 0 OR NOT _gentest_cdash_build_rc EQUAL 0 OR NOT _gentest_cdash_build_errors EQUAL 0)
            message(FATAL_ERROR
                "gentest-cdash: build failed for target '${_gentest_cdash_build_target}' (rc=${_gentest_cdash_build_rc}, errors=${_gentest_cdash_build_errors}, warnings=${_gentest_cdash_build_warnings}, cmake_error=${_gentest_cdash_build_err})")
        endif()
    endforeach()
endif()

if(_gentest_cdash_test_regex STREQUAL "")
    ctest_test(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        PARALLEL_LEVEL "${_gentest_cdash_parallel_level}"
        RETURN_VALUE _gentest_cdash_test_rc
        CAPTURE_CMAKE_ERROR _gentest_cdash_test_err)
else()
    ctest_test(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        PARALLEL_LEVEL "${_gentest_cdash_parallel_level}"
        INCLUDE "${_gentest_cdash_test_regex}"
        RETURN_VALUE _gentest_cdash_test_rc
        CAPTURE_CMAKE_ERROR _gentest_cdash_test_err)
endif()
if(_gentest_cdash_test_err LESS 0 OR NOT _gentest_cdash_test_rc EQUAL 0)
    message(FATAL_ERROR "gentest-cdash: test step failed (rc=${_gentest_cdash_test_rc}, cmake_error=${_gentest_cdash_test_err})")
endif()

if(NOT _gentest_cdash_submit_url STREQUAL "")
    ctest_submit(
        PARTS Start Configure Build Test Done
        SUBMIT_URL "${_gentest_cdash_submit_url}"
        RETURN_VALUE _gentest_cdash_submit_rc)
    if(NOT _gentest_cdash_submit_rc EQUAL 0)
        message(FATAL_ERROR "gentest-cdash: submit failed (rc=${_gentest_cdash_submit_rc})")
    endif()
endif()
