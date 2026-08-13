cmake_minimum_required(VERSION 3.31)
project(gentest_header_declaration_registration LANGUAGES CXX)

enable_testing()

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/header_a_symlink.hpp")
    file(CREATE_LINK
        "${CMAKE_CURRENT_SOURCE_DIR}/header_a.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/header_a_symlink.hpp"
        SYMBOLIC
        COPY_ON_ERROR
        RESULT _header_alias_result)
    if(NOT _header_alias_result STREQUAL "0")
        message(FATAL_ERROR "Failed to create the header alias: ${_header_alias_result}")
    endif()
endif()
if(IS_SYMLINK "${CMAKE_CURRENT_SOURCE_DIR}/header_a_symlink.hpp")
    set(_header_alias_is_symlink 1)
else()
    set(_header_alias_is_symlink 0)
endif()

gentest_add_mocks(header_registration_mocks
    DEFS ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_mocks"
    HEADER_NAME public/header_registration_mocks.hpp)

add_executable(header_declaration_registration_tests
    cases_a.cpp
    cases_b.cpp
    cases_c.cpp
    cases_rich.cpp
    contextual_cases.hpp
    fallback_cases.hpp
    header_a.hpp
    header_b.hpp
    inline_cases.hpp
    macro_cases.hpp
    mock_cases.cpp
    mock_cases.hpp
    redeclaration_a.hpp
    redeclaration_b.hpp
    rich_cases.hpp)
target_link_libraries(header_declaration_registration_tests PRIVATE gentest_main)
gentest_link_mocks(header_declaration_registration_tests PRIVATE header_registration_mocks)
target_compile_features(header_declaration_registration_tests PRIVATE cxx_std_20)
set_property(TARGET header_declaration_registration_tests PROPERTY UNITY_BUILD ON)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_forced_context_option "/FI${CMAKE_CURRENT_SOURCE_DIR}/forced_context.hpp")
else()
    set(_forced_context_option "-include;${CMAKE_CURRENT_SOURCE_DIR}/forced_context.hpp")
endif()
set_source_files_properties(cases_a.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/slot_a"
    COMPILE_OPTIONS "${_forced_context_option}")
set_source_files_properties(cases_b.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/slot_b"
    COMPILE_OPTIONS "${_forced_context_option}")
set_source_files_properties(cases_c.cpp PROPERTIES
    COMPILE_DEFINITIONS "GENTEST_HAVE_HEADER_ALIAS_SYMLINK=${_header_alias_is_symlink}")
gentest_attach_codegen(header_declaration_registration_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
gentest_discover_tests(header_declaration_registration_tests TEST_PREFIX "additive::")

get_target_property(_header_registration_sources header_declaration_registration_tests SOURCES)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/target_sources.txt" "${_header_registration_sources}\n")

# Compare the next-major additive inventory with the previous-major internal
# wrapper compatibility path over the same authored declarations.
add_executable(header_declaration_wrapper_compat_tests
    wrapper_compat_cases.cpp)
target_link_libraries(header_declaration_wrapper_compat_tests PRIVATE gentest_main)
target_compile_features(header_declaration_wrapper_compat_tests PRIVATE cxx_std_20)
set_property(TARGET header_declaration_wrapper_compat_tests PROPERTY GENTEST_INTERNAL_TEXTUAL_WRAPPER_COMPATIBILITY ON)
gentest_attach_codegen(header_declaration_wrapper_compat_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_wrapper_compat")
gentest_discover_tests(header_declaration_wrapper_compat_tests TEST_PREFIX "compat::")

foreach(_dual_variant IN ITEMS 1 2)
    set(_dual_target "header_declaration_dual_${_dual_variant}")
    add_executable(${_dual_target} dual_target.cpp dual_target.hpp)
    target_link_libraries(${_dual_target} PRIVATE gentest_main)
    target_compile_features(${_dual_target} PRIVATE cxx_std_20)
    target_compile_definitions(${_dual_target} PRIVATE "DUAL_TARGET_VARIANT=${_dual_variant}")
    gentest_attach_codegen(${_dual_target}
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_dual_${_dual_variant}")
endforeach()
