add_executable(gentest_core_tests
    parse_validate_tests.cpp
    ../src/parse_core.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_parse_validate
    COMMAND gentest_core_tests)

if(TARGET gentest_codegen_log_support)
    add_executable(gentest_core_log_tests
        log_tests.cpp)

    target_compile_features(gentest_core_log_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_log_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(gentest_core_log_tests PRIVATE gentest_codegen_log_support)

    add_test(NAME gentest_core_log
        COMMAND gentest_core_log_tests)
endif()

add_executable(gentest_core_type_kind_tests
    type_kind_tests.cpp
    ../src/type_kind.cpp
    ../src/render.cpp)

target_compile_features(gentest_core_type_kind_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_type_kind_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_type_kind_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_type_kind
    COMMAND gentest_core_type_kind_tests)

add_executable(gentest_core_render_tests
    render_tests.cpp
    ../src/render.cpp)

target_compile_features(gentest_core_render_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_render_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_render
    COMMAND gentest_core_render_tests)

if(TARGET gentest_codegen_llvm_support)
    add_executable(gentest_core_discovery_utils_tests
        discovery_utils_tests.cpp)

    target_compile_features(gentest_core_discovery_utils_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_discovery_utils_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(gentest_core_discovery_utils_tests PRIVATE gentest_codegen_llvm_support)

    add_test(NAME gentest_core_discovery_utils
        COMMAND gentest_core_discovery_utils_tests)
endif()

if(TARGET gentest_codegen_support)
    add_executable(gentest_core_discovery_tests
        discovery_tests.cpp
        ../src/discovery.cpp
        ../src/parse.cpp
        ../src/parse_core.cpp
        ../src/render.cpp
        ../src/type_kind.cpp
        ../src/validate.cpp)

    target_compile_features(gentest_core_discovery_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_discovery_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(gentest_core_discovery_tests PRIVATE gentest_codegen_support)
    if(WIN32 AND MSVC)
        set_property(TARGET gentest_core_discovery_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()

    add_test(NAME gentest_core_discovery
        COMMAND gentest_core_discovery_tests)
endif()
