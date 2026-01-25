add_library(${PROJECT_NAME} INTERFACE)

# Linkage selection: default STATIC, optional SHARED
option(GENTEST_RUNTIME_SHARED "Build gentest_runtime as a shared library" OFF)
set(_gentest_runtime_libtype STATIC)
if(GENTEST_RUNTIME_SHARED)
    set(_gentest_runtime_libtype SHARED)
endif()

add_library(gentest_runtime ${_gentest_runtime_libtype}
    ${PROJECT_SOURCE_DIR}/src/runner_impl.cpp)

add_library(gentest_main STATIC
    ${PROJECT_SOURCE_DIR}/src/gentest_main.cpp)

target_link_libraries(gentest_main
    PUBLIC
        ${PROJECT_NAME})

if(GENTEST_RUNTIME_SHARED)
    # Only set PIC for the shared case
    set_target_properties(gentest_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Make Windows shared builds work without explicit __declspec(dllexport)
    if(WIN32)
        set_target_properties(gentest_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

target_link_libraries(gentest_runtime
    PUBLIC fmt::fmt)

target_compile_features(gentest_runtime PUBLIC cxx_std_20)

# For LLVM 21+ with libc++, explicitly link LLVM's libc++ to avoid ABI issues
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
    # Detect if we're using Homebrew LLVM (not Apple Clang)
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_compiler_dir}/.." ABSOLUTE)
    # Check if this is Homebrew LLVM by looking for the libc++ in the LLVM tree
    if(EXISTS "${_llvm_prefix}/lib/c++/libc++.dylib")
        target_link_libraries(gentest_runtime PUBLIC "${_llvm_prefix}/lib/c++/libc++.dylib")
        message(STATUS "gentest_runtime: linking LLVM libc++: ${_llvm_prefix}/lib/c++/libc++.dylib")
    endif()
endif()

# Glob all header files
file(GLOB _gentest_headers
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.h
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.hpp
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.inl
)

# Publish headers for dependents (and install/export them via FILE_SET)
target_sources(gentest_runtime
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES ${_gentest_headers}
)

target_link_libraries(${PROJECT_NAME}
    INTERFACE gentest_runtime)

target_compile_options(${PROJECT_NAME}
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,AppleClang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd5030>)

if(NOT TARGET gentest::gentest)
    add_library(gentest::gentest ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET gentest::gentest_runtime)
    add_library(gentest::gentest_runtime ALIAS gentest_runtime)
endif()
if(NOT TARGET gentest::gentest_main)
    add_library(gentest::gentest_main ALIAS gentest_main)
endif()
