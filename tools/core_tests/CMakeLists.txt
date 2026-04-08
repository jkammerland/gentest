add_executable(gentest_core_tests
    parse_validate_tests.cpp
    ../src/parse_core.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_parse_validate
    COMMAND gentest_core_tests)

add_executable(gentest_core_log_tests
    log_tests.cpp)

target_compile_features(gentest_core_log_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_log_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(_gentest_core_log_llvm_support Support Demangle)
target_link_libraries(gentest_core_log_tests PRIVATE fmt::fmt ${_gentest_core_log_llvm_support})

add_test(NAME gentest_core_log
    COMMAND gentest_core_log_tests)
