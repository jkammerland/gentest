set(_gentest_test_suites
    unit
    integration)

foreach(suite IN LISTS _gentest_test_suites)
    set(target_name "${PROJECT_NAME}_${suite}_tests")

    add_executable(${target_name}
        support/test_entry.cpp
        ${suite}/cases.cpp)

    target_link_libraries(${target_name}
        PRIVATE
            ${PROJECT_NAME}_runtime)

    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_compile_options(${target_name} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-unknown-attributes>)

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
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME ${suite}
        COMMAND ${target_name})
endforeach()

unset(_gentest_test_suites)
