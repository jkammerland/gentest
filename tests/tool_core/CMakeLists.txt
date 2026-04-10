function(_gentest_copy_codegen_runtime_settings target)
    if(NOT TARGET gentest_codegen)
        return()
    endif()

    get_target_property(_gentest_codegen_compile_defs gentest_codegen COMPILE_DEFINITIONS)
    if(_gentest_codegen_compile_defs AND NOT _gentest_codegen_compile_defs STREQUAL "_gentest_codegen_compile_defs-NOTFOUND")
        target_compile_definitions(${target} PRIVATE ${_gentest_codegen_compile_defs})
    endif()

    get_target_property(_gentest_codegen_compile_opts gentest_codegen COMPILE_OPTIONS)
    if(_gentest_codegen_compile_opts AND NOT _gentest_codegen_compile_opts STREQUAL "_gentest_codegen_compile_opts-NOTFOUND")
        target_compile_options(${target} PRIVATE ${_gentest_codegen_compile_opts})
    endif()

    get_target_property(_gentest_codegen_link_opts gentest_codegen LINK_OPTIONS)
    if(_gentest_codegen_link_opts AND NOT _gentest_codegen_link_opts STREQUAL "_gentest_codegen_link_opts-NOTFOUND")
        target_link_options(${target} PRIVATE ${_gentest_codegen_link_opts})
    endif()

    get_target_property(_gentest_codegen_build_rpath gentest_codegen BUILD_RPATH)
    if(_gentest_codegen_build_rpath AND NOT _gentest_codegen_build_rpath STREQUAL "_gentest_codegen_build_rpath-NOTFOUND")
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${_gentest_codegen_build_rpath}")
    endif()

    get_target_property(_gentest_codegen_msvc_runtime gentest_codegen MSVC_RUNTIME_LIBRARY)
    if(_gentest_codegen_msvc_runtime AND NOT _gentest_codegen_msvc_runtime STREQUAL "_gentest_codegen_msvc_runtime-NOTFOUND")
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "${_gentest_codegen_msvc_runtime}")
    endif()
endfunction()

set(_gentest_tool_core_src_dir "${PROJECT_SOURCE_DIR}/tools/src")

add_executable(gentest_core_tests
    parse_validate_tests.cpp
    ${_gentest_tool_core_src_dir}/parse_core.cpp
    ${_gentest_tool_core_src_dir}/validate.cpp)

target_compile_features(gentest_core_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_tests PRIVATE ${_gentest_tool_core_src_dir})
target_link_libraries(gentest_core_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_parse_validate
    COMMAND gentest_core_tests)

if(TARGET gentest_codegen_log_support)
    add_executable(gentest_core_log_tests
        log_tests.cpp)

    target_compile_features(gentest_core_log_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_log_tests PRIVATE ${_gentest_tool_core_src_dir})
    target_link_libraries(gentest_core_log_tests PRIVATE gentest_codegen_log_support)
    _gentest_copy_codegen_runtime_settings(gentest_core_log_tests)

    add_test(NAME gentest_core_log
        COMMAND gentest_core_log_tests)
endif()

add_executable(gentest_core_type_kind_tests
    type_kind_tests.cpp
    ${_gentest_tool_core_src_dir}/type_kind.cpp
    ${_gentest_tool_core_src_dir}/render.cpp)

target_compile_features(gentest_core_type_kind_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_type_kind_tests PRIVATE ${_gentest_tool_core_src_dir})
target_link_libraries(gentest_core_type_kind_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_type_kind
    COMMAND gentest_core_type_kind_tests)

add_executable(gentest_core_render_tests
    render_tests.cpp
    ${_gentest_tool_core_src_dir}/render.cpp)

target_compile_features(gentest_core_render_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_render_tests PRIVATE ${_gentest_tool_core_src_dir})
target_link_libraries(gentest_core_render_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_render
    COMMAND gentest_core_render_tests)

if(TARGET gentest_codegen_support)
    add_executable(gentest_core_discovery_utils_tests
        discovery_utils_tests.cpp)

    target_compile_features(gentest_core_discovery_utils_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_discovery_utils_tests PRIVATE ${_gentest_tool_core_src_dir})
    target_link_libraries(gentest_core_discovery_utils_tests PRIVATE gentest_codegen_support)
    _gentest_copy_codegen_runtime_settings(gentest_core_discovery_utils_tests)

    add_test(NAME gentest_core_discovery_utils
        COMMAND gentest_core_discovery_utils_tests)
endif()

if(TARGET gentest_codegen_support)
    add_executable(gentest_core_discovery_tests
        discovery_tests.cpp
        ${_gentest_tool_core_src_dir}/discovery.cpp
        ${_gentest_tool_core_src_dir}/parse.cpp
        ${_gentest_tool_core_src_dir}/parse_core.cpp
        ${_gentest_tool_core_src_dir}/render.cpp
        ${_gentest_tool_core_src_dir}/type_kind.cpp
        ${_gentest_tool_core_src_dir}/validate.cpp)

    target_compile_features(gentest_core_discovery_tests PRIVATE cxx_std_20)
    target_include_directories(gentest_core_discovery_tests PRIVATE
        ${_gentest_tool_core_src_dir}
        ${LLVM_INCLUDE_DIRS})
    target_link_libraries(gentest_core_discovery_tests PRIVATE gentest_codegen_support)
    _gentest_copy_codegen_runtime_settings(gentest_core_discovery_tests)

    add_test(NAME gentest_core_discovery
        COMMAND gentest_core_discovery_tests)
endif()

unset(_gentest_tool_core_src_dir)
