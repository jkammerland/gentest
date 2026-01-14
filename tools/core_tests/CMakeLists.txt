add_executable(gentest_core_tests
    parse_validate_tests.cpp
    ../src/parse_core.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_parse_validate
    COMMAND gentest_core_tests)

add_executable(gentest_path_utils_tests
    path_utils_tests.cpp)
target_compile_features(gentest_path_utils_tests PRIVATE cxx_std_20)
target_include_directories(gentest_path_utils_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME gentest_core_path_utils
    COMMAND gentest_path_utils_tests)
