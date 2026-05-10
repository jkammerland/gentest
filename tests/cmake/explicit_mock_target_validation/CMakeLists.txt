cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_validation LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()
if(NOT DEFINED TEST_CASE OR "${TEST_CASE}" STREQUAL "")
    message(FATAL_ERROR "TEST_CASE must be set")
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

if(TEST_CASE STREQUAL "duplicate_output_dir")
    gentest_add_mocks(explicit_validation_a
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shared")

    gentest_add_mocks(explicit_validation_b
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_b.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shared")
elseif(TEST_CASE STREQUAL "textual_module_name")
    gentest_add_mocks(explicit_validation_textual_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_textual)
elseif(TEST_CASE STREQUAL "module_header_name")
    gentest_add_mocks(explicit_validation_module_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/good_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_module_header
        HEADER_NAME public/fixture_validation.hpp)
elseif(TEST_CASE STREQUAL "mixed_surface_defs")
    gentest_add_mocks(explicit_validation_mixed_defs
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/good_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_mixed_defs)
elseif(TEST_CASE STREQUAL "absolute_header_name")
    gentest_add_mocks(explicit_validation_absolute_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "${CMAKE_CURRENT_BINARY_DIR}/absolute.hpp")
elseif(TEST_CASE STREQUAL "escaped_header_name")
    gentest_add_mocks(explicit_validation_escaped_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "../escape.hpp")
elseif(TEST_CASE STREQUAL "reserved_header_name")
    gentest_add_mocks(explicit_validation_reserved_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "explicit_validation_reserved_header_mock_impl.hpp")
elseif(TEST_CASE STREQUAL "generator_expression_defs")
    gentest_add_mocks(explicit_validation_genex_defs
        DEFS
            "$<CONFIG>/defs_a.hpp"
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
elseif(TEST_CASE STREQUAL "generator_expression_output_dir")
    gentest_add_mocks(explicit_validation_genex_output_dir
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "$<CONFIG>/generated")
elseif(TEST_CASE STREQUAL "unsupported_include_root_genex")
    add_library(explicit_validation_genex_include_root INTERFACE)
    target_include_directories(explicit_validation_genex_include_root INTERFACE
        "$<IF:$<BOOL:1>,${CMAKE_CURRENT_SOURCE_DIR},${CMAKE_CURRENT_BINARY_DIR}>")

    gentest_add_mocks(explicit_validation_genex_include_root_mocks
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        LINK_LIBRARIES explicit_validation_genex_include_root)
elseif(TEST_CASE STREQUAL "invalid_mock_backend")
    gentest_add_mocks(explicit_validation_invalid_backend
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        BACKEND nope)
elseif(TEST_CASE STREQUAL "third_party_module_backend")
    gentest_add_mocks(explicit_validation_third_party_module_backend
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/good_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_module_backend
        BACKEND gmock)
elseif(TEST_CASE STREQUAL "third_party_textual_backend_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_textual_backend
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_consumer third_party_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_consumer PRIVATE explicit_validation_third_party_textual_backend)
elseif(TEST_CASE STREQUAL "third_party_overload_order_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_overload_order
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_overload_order_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_overload_order_consumer third_party_overload_order_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_overload_order_consumer PRIVATE
        explicit_validation_third_party_overload_order)
elseif(TEST_CASE STREQUAL "third_party_defs_include_mock_h")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_defs_include_mock_h
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_defs_include_mock_h.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_consumer third_party_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_consumer PRIVATE explicit_validation_third_party_defs_include_mock_h)
elseif(TEST_CASE STREQUAL "third_party_same_file_defs_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_same_file
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_same_file_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_same_file_consumer third_party_same_file_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_same_file_consumer PRIVATE explicit_validation_third_party_same_file)
elseif(TEST_CASE STREQUAL "third_party_nested_target_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_nested
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_nested_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_nested_consumer third_party_nested_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_nested_consumer PRIVATE explicit_validation_third_party_nested)
elseif(TEST_CASE STREQUAL "third_party_template_target_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_template
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_template_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_template_consumer third_party_template_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_template_consumer PRIVATE explicit_validation_third_party_template)
elseif(TEST_CASE STREQUAL "third_party_variadic_method_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_variadic
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_variadic_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_variadic_consumer third_party_variadic_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_variadic_consumer PRIVATE explicit_validation_third_party_variadic)
elseif(TEST_CASE STREQUAL "third_party_conversion_operator_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_conversion
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_conversion_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_conversion_consumer third_party_conversion_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_conversion_consumer PRIVATE explicit_validation_third_party_conversion)
elseif(TEST_CASE STREQUAL "third_party_overloaded_operator_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_operator
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_operator_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_operator_consumer third_party_operator_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_operator_consumer PRIVATE explicit_validation_third_party_operator)
elseif(TEST_CASE STREQUAL "third_party_static_method_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_static_method
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_static_method_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_static_method_consumer third_party_static_method_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_static_method_consumer PRIVATE explicit_validation_third_party_static_method)
elseif(TEST_CASE STREQUAL "third_party_member_template_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_member_template
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_member_template_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_member_template_consumer third_party_member_template_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_member_template_consumer PRIVATE explicit_validation_third_party_member_template)
elseif(TEST_CASE STREQUAL "third_party_volatile_method_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_volatile
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_volatile_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_volatile_consumer third_party_volatile_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_volatile_consumer PRIVATE explicit_validation_third_party_volatile)
elseif(TEST_CASE STREQUAL "third_party_final_target_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_final_target
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_final_target_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_final_target_consumer third_party_final_target_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_final_target_consumer PRIVATE explicit_validation_third_party_final_target)
elseif(TEST_CASE STREQUAL "third_party_final_method_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_final_method
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_final_method_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_final_method_consumer third_party_final_method_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_final_method_consumer PRIVATE explicit_validation_third_party_final_method)
elseif(TEST_CASE STREQUAL "third_party_private_pure_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_private_pure
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_private_pure_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_private_pure_consumer third_party_private_pure_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_private_pure_consumer PRIVATE explicit_validation_third_party_private_pure)
elseif(TEST_CASE STREQUAL "third_party_inherited_public_pure_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_inherited_public_pure
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_inherited_public_pure_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_inherited_public_pure_consumer third_party_inherited_public_pure_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_inherited_public_pure_consumer PRIVATE
        explicit_validation_third_party_inherited_public_pure)
elseif(TEST_CASE STREQUAL "third_party_inherited_private_pure_rejected")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_inherited_private_pure
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_inherited_private_pure_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_inherited_private_pure_consumer third_party_inherited_private_pure_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_inherited_private_pure_consumer PRIVATE
        explicit_validation_third_party_inherited_private_pure)
elseif(TEST_CASE STREQUAL "third_party_method_default_args_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_method_default_args
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_method_default_args_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_method_default_args_consumer third_party_method_default_args_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_method_default_args_consumer PRIVATE
        explicit_validation_third_party_method_default_args)
elseif(TEST_CASE STREQUAL "third_party_ctor_default_args_surface")
    add_library(explicit_validation_fake_gmock INTERFACE)
    target_include_directories(explicit_validation_fake_gmock INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/fake_gmock")

    gentest_add_mocks(explicit_validation_third_party_ctor_default_args
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party_ctor_default_args_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/fixture_validation.hpp
        BACKEND gmock
        LINK_LIBRARIES explicit_validation_fake_gmock)

    add_executable(explicit_validation_third_party_ctor_default_args_consumer third_party_ctor_default_args_consumer.cpp)
    target_link_libraries(explicit_validation_third_party_ctor_default_args_consumer PRIVATE
        explicit_validation_third_party_ctor_default_args)
elseif(TEST_CASE STREQUAL "native_default_args_surface")
    gentest_add_mocks(explicit_validation_native_default_args
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_default_args_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_default_args.hpp)

    add_executable(explicit_validation_native_default_args_consumer native_default_args_consumer.cpp)
    target_link_libraries(explicit_validation_native_default_args_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_default_args)
    gentest_attach_codegen(explicit_validation_native_default_args_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_default_args")
elseif(TEST_CASE STREQUAL "native_inherited_concrete_surface")
    gentest_add_mocks(explicit_validation_native_inherited_concrete
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_inherited_concrete_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_inherited_concrete.hpp)

    add_executable(explicit_validation_native_inherited_concrete_consumer native_inherited_concrete_consumer.cpp)
    target_link_libraries(explicit_validation_native_inherited_concrete_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_inherited_concrete)
    gentest_attach_codegen(explicit_validation_native_inherited_concrete_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_inherited_concrete")
elseif(TEST_CASE STREQUAL "native_generic_alias_template_surface")
    gentest_add_mocks(explicit_validation_native_generic_alias
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_generic_alias_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_generic_alias.hpp)

    add_executable(explicit_validation_native_generic_alias_consumer native_generic_alias_consumer.cpp)
    target_link_libraries(explicit_validation_native_generic_alias_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_generic_alias)
    gentest_attach_codegen(explicit_validation_native_generic_alias_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_generic_alias")
elseif(TEST_CASE STREQUAL "native_conversion_operator_rejected")
    gentest_add_mocks(explicit_validation_native_conversion
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_conversion_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_conversion)
elseif(TEST_CASE STREQUAL "native_variadic_method_rejected")
    gentest_add_mocks(explicit_validation_native_variadic
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_variadic_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_variadic)
elseif(TEST_CASE STREQUAL "native_volatile_method_rejected")
    gentest_add_mocks(explicit_validation_native_volatile
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_volatile_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_volatile)
elseif(TEST_CASE STREQUAL "native_pure_assignment_rejected")
    gentest_add_mocks(explicit_validation_native_assignment
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_pure_assignment_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_assignment)
elseif(TEST_CASE STREQUAL "native_deleted_default_ctor_rejected")
    gentest_add_mocks(explicit_validation_native_deleted_default
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_deleted_default_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_deleted_default)
elseif(TEST_CASE STREQUAL "native_ignored_unsupported_surface")
    gentest_add_mocks(explicit_validation_native_ignored_unsupported
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_ignored_unsupported_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_ignored_unsupported.hpp)

    add_executable(explicit_validation_native_ignored_unsupported_consumer native_ignored_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_ignored_unsupported_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_ignored_unsupported)
    gentest_attach_codegen(explicit_validation_native_ignored_unsupported_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_ignored_unsupported")
elseif(TEST_CASE STREQUAL "native_inherited_final_surface")
    gentest_add_mocks(explicit_validation_native_inherited_final
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_inherited_final_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_inherited_final.hpp)

    add_executable(explicit_validation_native_inherited_final_consumer native_inherited_final_consumer.cpp)
    target_link_libraries(explicit_validation_native_inherited_final_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_inherited_final)
    gentest_attach_codegen(explicit_validation_native_inherited_final_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_inherited_final")
elseif(TEST_CASE STREQUAL "native_final_pure_method_rejected")
    gentest_add_mocks(explicit_validation_native_final_pure
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_final_pure_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_unsupported.hpp)

    add_executable(explicit_validation_native_unsupported_consumer native_unsupported_consumer.cpp)
    target_link_libraries(explicit_validation_native_unsupported_consumer PRIVATE explicit_validation_native_final_pure)
elseif(TEST_CASE STREQUAL "native_const_record_ctor_rejected")
    gentest_add_mocks(explicit_validation_native_const_record_ctor
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_const_record_ctor_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_const_record_ctor.hpp)

    add_executable(explicit_validation_native_const_record_ctor_consumer native_const_record_ctor_consumer.cpp)
    target_link_libraries(explicit_validation_native_const_record_ctor_consumer PRIVATE explicit_validation_native_const_record_ctor)
elseif(TEST_CASE STREQUAL "native_default_arg_namespace_surface")
    gentest_add_mocks(explicit_validation_native_default_arg_namespace
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_default_arg_namespace_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_default_arg_namespace.hpp)

    add_executable(explicit_validation_native_default_arg_namespace_consumer native_default_arg_namespace_consumer.cpp)
    target_link_libraries(explicit_validation_native_default_arg_namespace_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_default_arg_namespace)
    gentest_attach_codegen(explicit_validation_native_default_arg_namespace_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_default_arg_namespace")
elseif(TEST_CASE STREQUAL "native_default_arg_type_surface")
    gentest_add_mocks(explicit_validation_native_default_arg_type
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_default_arg_type_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_default_arg_type.hpp)

    add_executable(explicit_validation_native_default_arg_type_consumer native_default_arg_type_consumer.cpp)
    target_link_libraries(explicit_validation_native_default_arg_type_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_default_arg_type)
    gentest_attach_codegen(explicit_validation_native_default_arg_type_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_default_arg_type")
elseif(TEST_CASE STREQUAL "native_ambiguous_zero_arg_ctor_surface")
    gentest_add_mocks(explicit_validation_native_ambiguous_zero_arg_ctor
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/native_ambiguous_zero_arg_ctor_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME public/native_ambiguous_zero_arg_ctor.hpp)

    add_executable(explicit_validation_native_ambiguous_zero_arg_ctor_consumer native_ambiguous_zero_arg_ctor_consumer.cpp)
    target_link_libraries(explicit_validation_native_ambiguous_zero_arg_ctor_consumer PRIVATE
        gentest::gentest_main
        explicit_validation_native_ambiguous_zero_arg_ctor)
    gentest_attach_codegen(explicit_validation_native_ambiguous_zero_arg_ctor_consumer
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_tests/native_ambiguous_zero_arg_ctor")
elseif(TEST_CASE STREQUAL "missing_named_module")
    gentest_add_mocks(explicit_validation_missing_named_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/bad_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_missing_named_module)
elseif(TEST_CASE STREQUAL "provider_only_module")
    gentest_add_mocks(explicit_validation_provider_only_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/provider_only_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_provider_only_module)
elseif(TEST_CASE STREQUAL "implementation_unit_module")
    gentest_add_mocks(explicit_validation_implementation_unit_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/implementation_unit_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_implementation_unit_module)
elseif(TEST_CASE STREQUAL "partition_module")
    gentest_add_mocks(explicit_validation_partition_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/partition_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_partition_module)
elseif(TEST_CASE STREQUAL "missing_module_name")
    gentest_add_mocks(explicit_validation_missing_module_name
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/good_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
elseif(TEST_CASE STREQUAL "nested_module_class_scope")
    gentest_add_mocks(explicit_validation_nested_scope
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/nested_module_class_scope.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_nested_scope)
else()
    message(FATAL_ERROR "Unknown TEST_CASE='${TEST_CASE}'")
endif()
