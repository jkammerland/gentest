set(_gentest_test_suites
    unit
    integration
    failing)

foreach(suite IN LISTS _gentest_test_suites)
    set(target_name "${PROJECT_NAME}_${suite}_tests")

    add_executable(${target_name}
        support/test_entry.cpp
        ${suite}/cases.cpp)

    target_link_libraries(${target_name}
        PRIVATE
            ${PROJECT_NAME})

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
        -DPASS=0 -DFAIL=1 -DSKIP=0
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTestCounts.cmake)

# The raw failing suite run is expected to fail
set_tests_properties(failing PROPERTIES WILL_FAIL TRUE)
