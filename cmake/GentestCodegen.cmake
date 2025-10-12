function(gentest_attach_codegen target)
    set(options)
    set(one_value_args OUTPUT ENTRY)
    set(multi_value_args SOURCES CLANG_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_SOURCES)
        message(FATAL_ERROR "gentest_attach_codegen requires SOURCES")
    endif()

    if(NOT GENTEST_ENTRY)
        set(GENTEST_ENTRY gentest::run_all_tests)
    endif()

    if(NOT GENTEST_OUTPUT)
        set(GENTEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}_generated.cpp")
    endif()

    # Allow external prebuilt generator if the in-tree target is disabled
    set(_gentest_codegen_is_target FALSE)
    if(TARGET gentest_codegen)
        set(_gentest_codegen_is_target TRUE)
    else()
        # Honor explicit override or search PATH
        if(DEFINED GENTEST_CODEGEN AND GENTEST_CODEGEN)
            set(GENTEST_CODEGEN_EXE "${GENTEST_CODEGEN}")
        else()
            find_program(GENTEST_CODEGEN_EXE NAMES gentest_codegen)
        endif()
        if(NOT GENTEST_CODEGEN_EXE)
            message(FATAL_ERROR "gentest_codegen is not available. Enable -DGENTEST_BUILD_CODEGEN=ON or provide a prebuilt generator via PATH or -DGENTEST_CODEGEN=/path/to/gentest_codegen")
        endif()
    endif()

    get_filename_component(_gentest_output_dir "${GENTEST_OUTPUT}" DIRECTORY)
    if(_gentest_output_dir STREQUAL "")
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    set(_gentest_mock_registry "${_gentest_output_dir}/mock_registry.hpp")
    # Generate inline mock implementations as a header; it will be included
    # by the generated test implementation after including sources.
    set(_gentest_mock_impl "${_gentest_output_dir}/mock_impl.hpp")

    if(_gentest_codegen_is_target)
        set(_codegen_launcher $<TARGET_FILE:gentest_codegen>)
    else()
        set(_codegen_launcher ${GENTEST_CODEGEN_EXE})
    endif()

    set(_command ${_codegen_launcher}
        --output ${GENTEST_OUTPUT}
        --entry ${GENTEST_ENTRY}
        --mock-registry ${_gentest_mock_registry}
        --mock-impl ${_gentest_mock_impl}
        --compdb ${CMAKE_BINARY_DIR}
        ${GENTEST_SOURCES})

    list(APPEND _command --)
    list(APPEND _command -DGENTEST_CODEGEN=1)
    if(GENTEST_CLANG_ARGS)
        list(APPEND _command ${GENTEST_CLANG_ARGS})
    endif()

    add_custom_command(
        OUTPUT ${GENTEST_OUTPUT} ${_gentest_mock_registry} ${_gentest_mock_impl}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS ${GENTEST_SOURCES} ${GENTEST_DEPENDS} $<$<BOOL:${_gentest_codegen_is_target}>:gentest_codegen>
        COMMENT "Generating ${GENTEST_OUTPUT} for target ${target}"
        VERBATIM
        CODEGEN
    )

    # Only compile the generated test implementation; the mock impl header
    # is included by it and must not be compiled as a separate TU.
    target_sources(${target} PRIVATE ${GENTEST_OUTPUT})

    get_filename_component(_gentest_mock_dir "${_gentest_mock_registry}" DIRECTORY)
    target_include_directories(${target} PRIVATE ${_gentest_mock_dir})

    get_filename_component(_gentest_mock_header_name "${_gentest_mock_registry}" NAME)
    get_filename_component(_gentest_mock_impl_name "${_gentest_mock_impl}" NAME)
    target_compile_definitions(${target} PRIVATE
        GENTEST_MOCK_REGISTRY_PATH=${_gentest_mock_header_name}
        GENTEST_MOCK_IMPL_PATH=${_gentest_mock_impl_name}
    )
    if(GENTEST_USE_BOOST_JSON)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_JSON)
    endif()
    if(GENTEST_USE_BOOST_UUID)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_UUID)
    endif()
    if(_gentest_codegen_is_target)
        add_dependencies(${target} gentest_codegen)
    endif()
endfunction()
