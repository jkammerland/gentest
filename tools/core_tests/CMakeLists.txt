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
if(TARGET clang-cpp)
    target_link_libraries(gentest_core_log_tests PRIVATE fmt::fmt clang-cpp)
    if(TARGET Terminfo::terminfo)
        target_link_libraries(gentest_core_log_tests PRIVATE Terminfo::terminfo)
    endif()
    if(_gentest_llvm_dso)
        target_link_libraries(gentest_core_log_tests PRIVATE "${_gentest_llvm_dso}")
        if(UNIX)
            set_property(TARGET gentest_core_log_tests APPEND PROPERTY BUILD_RPATH "${_gentest_llvm_dso_dir}")
        endif()
    endif()
else()
    llvm_map_components_to_libnames(_gentest_core_log_llvm_support Support Demangle)
    target_link_libraries(gentest_core_log_tests PRIVATE fmt::fmt ${_gentest_core_log_llvm_support})
endif()

add_test(NAME gentest_core_log
    COMMAND gentest_core_log_tests)

add_executable(gentest_core_type_kind_tests
    type_kind_tests.cpp
    ../src/type_kind.cpp
    ../src/render.cpp)

target_compile_features(gentest_core_type_kind_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_type_kind_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_type_kind_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_type_kind
    COMMAND gentest_core_type_kind_tests)
