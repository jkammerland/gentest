cmake_minimum_required(VERSION 3.20)

set(GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR "" CACHE STRING
    "Expected LLVM/Clang major version for the gentest clang-tidy plugin")

find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)

if(NOT TARGET clangTidy AND NOT TARGET clang-tidy)
    message(STATUS "gentest_tidy: clang-tidy development libraries not found; skipping plugin build")
    return()
endif()

if(NOT "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}" STREQUAL "")
    set(_gentest_tidy_llvm_major "")
    if(DEFINED LLVM_PACKAGE_VERSION AND NOT "${LLVM_PACKAGE_VERSION}" STREQUAL "")
        string(REGEX MATCH "^[0-9]+" _gentest_tidy_llvm_major "${LLVM_PACKAGE_VERSION}")
    elseif(DEFINED LLVM_VERSION_MAJOR AND NOT "${LLVM_VERSION_MAJOR}" STREQUAL "")
        set(_gentest_tidy_llvm_major "${LLVM_VERSION_MAJOR}")
    endif()

    if("${_gentest_tidy_llvm_major}" STREQUAL "")
        message(STATUS "gentest_tidy: unable to determine LLVM package major; skipping plugin build")
        return()
    endif()

    if(NOT _gentest_tidy_llvm_major STREQUAL GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR)
        message(STATUS
            "gentest_tidy: LLVM package major ${_gentest_tidy_llvm_major} does not match expected "
            "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}; skipping plugin build")
        return()
    endif()

    if(DEFINED CLANG_PACKAGE_VERSION AND NOT "${CLANG_PACKAGE_VERSION}" STREQUAL "")
        string(REGEX MATCH "^[0-9]+" _gentest_tidy_clang_major "${CLANG_PACKAGE_VERSION}")
        if(NOT "${_gentest_tidy_clang_major}" STREQUAL ""
           AND NOT _gentest_tidy_clang_major STREQUAL GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR)
            message(STATUS
                "gentest_tidy: Clang package major ${_gentest_tidy_clang_major} does not match expected "
                "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}; skipping plugin build")
            return()
        endif()
    endif()
endif()

set(_gentest_tidy_link_targets fmt::fmt)
foreach(_gentest_tidy_candidate IN ITEMS
    clangTidy
    clang-tidy
    clangTooling
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangLex
    clangSema
    clang-cpp
    LLVMSupport)
    if(TARGET ${_gentest_tidy_candidate})
        list(APPEND _gentest_tidy_link_targets ${_gentest_tidy_candidate})
    endif()
endforeach()

add_library(gentest_tidy MODULE
    GentestTidyModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parse_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/validate.cpp)

target_compile_features(gentest_tidy PRIVATE cxx_std_20)

target_include_directories(gentest_tidy
    PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(gentest_tidy
    PRIVATE
        ${_gentest_tidy_link_targets})

set_target_properties(gentest_tidy PROPERTIES
    OUTPUT_NAME "GentestTidyModule"
)
