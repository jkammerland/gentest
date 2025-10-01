cmake_minimum_required(VERSION 3.20)

if(NOT TARGET clangTidy)
    message(STATUS "gentest_tidy: clang-tidy libraries not found; skipping plugin build")
    return()
endif()

add_library(gentest_tidy MODULE
    GentestTidyModule.cpp)

target_compile_features(gentest_tidy PRIVATE cxx_std_20)

target_include_directories(gentest_tidy
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(gentest_tidy
    PRIVATE
        clangTidy
        clangTooling
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangLex
        clangSema
        LLVMSupport)

set_target_properties(gentest_tidy PROPERTIES
    OUTPUT_NAME "GentestTidyModule"
)

