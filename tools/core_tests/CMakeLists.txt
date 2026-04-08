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

add_executable(gentest_core_discovery_utils_tests
    discovery_utils_tests.cpp)

target_compile_features(gentest_core_discovery_utils_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_discovery_utils_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(_gentest_core_discovery_utils_llvm_support Support Demangle)
target_link_libraries(gentest_core_discovery_utils_tests PRIVATE ${_gentest_core_discovery_utils_llvm_support})

add_test(NAME gentest_core_discovery_utils
    COMMAND gentest_core_discovery_utils_tests)

function(_gentest_copy_codegen_runtime_settings target)
    get_target_property(_gentest_codegen_build_rpath gentest_codegen BUILD_RPATH)
    if(_gentest_codegen_build_rpath AND NOT _gentest_codegen_build_rpath STREQUAL "_gentest_codegen_build_rpath-NOTFOUND")
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${_gentest_codegen_build_rpath}")
    endif()

    get_target_property(_gentest_codegen_msvc_runtime gentest_codegen MSVC_RUNTIME_LIBRARY)
    if(_gentest_codegen_msvc_runtime AND NOT _gentest_codegen_msvc_runtime STREQUAL "_gentest_codegen_msvc_runtime-NOTFOUND")
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "${_gentest_codegen_msvc_runtime}")
    endif()
endfunction()

add_executable(gentest_core_discovery_tests
    discovery_tests.cpp
    ../src/discovery.cpp
    ../src/parse.cpp
    ../src/parse_core.cpp
    ../src/render.cpp
    ../src/type_kind.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_discovery_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_discovery_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS})
target_link_libraries(gentest_core_discovery_tests PRIVATE gentest_codegen_support)
_gentest_copy_codegen_runtime_settings(gentest_core_discovery_tests)

add_test(NAME gentest_core_discovery
    COMMAND gentest_core_discovery_tests)
