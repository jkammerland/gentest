cmake_minimum_required(VERSION 3.25)
project(gentest_llvm_sdk_probe LANGUAGES C CXX)

include("${GENTEST_SOURCE_DIR}/cmake/GentestLlvmDependencyShims.cmake")
gentest_prepare_llvm_package_imports()
find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)
gentest_normalize_llvm_imported_configs(${LLVM_AVAILABLE_LIBS} ${CLANG_EXPORTED_TARGETS})

# Compile and link against the actual SDK, including the component introduced
# in LLVM 23. Codegen's module integration tests exercise discovery itself.
add_executable(llvm_sdk_probe probe.cpp)
target_compile_features(llvm_sdk_probe PRIVATE cxx_std_20)
target_include_directories(llvm_sdk_probe SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
if(TARGET clang-cpp)
    # Distro SDKs may export only the shared Clang library.
    target_link_libraries(llvm_sdk_probe PRIVATE clang-cpp)
    if(TARGET LLVM)
        target_link_libraries(llvm_sdk_probe PRIVATE LLVM)
    else()
        target_link_libraries(llvm_sdk_probe PRIVATE LLVMSupport)
    endif()
else()
    target_link_libraries(llvm_sdk_probe PRIVATE clangIndex)
endif()
if(MSVC)
    # Match the runtime/STL ABI used by prebuilt LLVM SDK archives.
    set_property(TARGET llvm_sdk_probe PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
    target_compile_definitions(llvm_sdk_probe PRIVATE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
endif()
if(TARGET clangUnifiedSymbolResolution)
    target_link_libraries(llvm_sdk_probe PRIVATE clangUnifiedSymbolResolution)
endif()
