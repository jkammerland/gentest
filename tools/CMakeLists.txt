find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

add_executable(gentest_codegen
    src/main.cpp)

target_compile_features(gentest_codegen PRIVATE cxx_std_23)

target_compile_definitions(gentest_codegen
    PRIVATE
        GENTEST_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates")

if(DEFINED LLVM_INCLUDE_DIRS)
    target_include_directories(gentest_codegen PRIVATE ${LLVM_INCLUDE_DIRS})
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clang_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clang_includes})
    endif()
endif()

if(LLVM_ENABLE_RTTI)
    target_compile_definitions(gentest_codegen PRIVATE LLVM_ENABLE_RTTI)
endif()

set(_gentest_clang_libs
    clangTooling
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangLex
    clangParse
    clangSema)

llvm_map_components_to_libnames(_gentest_llvm_libs Support)

target_link_libraries(gentest_codegen
    PRIVATE
        ${_gentest_clang_libs}
        ${_gentest_llvm_libs})

unset(_gentest_llvm_libs)
unset(_gentest_clang_includes)
unset(_gentest_clang_libs)
