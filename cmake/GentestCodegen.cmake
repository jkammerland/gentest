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

    if(NOT TARGET gentest_codegen)
        message(FATAL_ERROR "gentest_codegen executable target is not available")
    endif()

    set(_command $<TARGET_FILE:gentest_codegen> --output ${GENTEST_OUTPUT} --entry ${GENTEST_ENTRY}
                 --compdb ${CMAKE_BINARY_DIR} ${GENTEST_SOURCES})

    if(GENTEST_CLANG_ARGS)
        list(APPEND _command --)
        list(APPEND _command ${GENTEST_CLANG_ARGS})
    endif()

    add_custom_command(
        OUTPUT ${GENTEST_OUTPUT}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS gentest_codegen ${GENTEST_SOURCES} ${GENTEST_DEPENDS}
        COMMENT "Generating ${GENTEST_OUTPUT} for target ${target}"
        VERBATIM
        CODEGEN
    )

    target_sources(${target} PRIVATE ${GENTEST_OUTPUT})
    add_dependencies(${target} gentest_codegen)
endfunction()
