add_library(${PROJECT_NAME} INTERFACE)

# Linkage selection: default STATIC, optional SHARED
option(GENTEST_RUNTIME_SHARED "Build gentest_runtime as a shared library" OFF)
set(_gentest_runtime_libtype STATIC)
if(GENTEST_RUNTIME_SHARED)
    set(_gentest_runtime_libtype SHARED)
endif()

add_library(gentest_runtime ${_gentest_runtime_libtype}
    ${PROJECT_SOURCE_DIR}/src/runner_impl.cpp)

if(GENTEST_RUNTIME_SHARED)
    # Only set PIC for the shared case
    set_target_properties(gentest_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Make Windows shared builds work without explicit __declspec(dllexport)
    if(WIN32)
        set_target_properties(gentest_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

target_include_directories(gentest_runtime
    PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(gentest_runtime
    PUBLIC fmt::fmt)

target_compile_features(gentest_runtime PUBLIC cxx_std_23)

set(_gentest_boost_components "")
set(_gentest_boost_links "")
if(GENTEST_USE_BOOST_JSON)
    list(APPEND _gentest_boost_components json)
    list(APPEND _gentest_boost_links Boost::json)
    target_compile_definitions(gentest_runtime PUBLIC GENTEST_USE_BOOST_JSON)
endif()
if(GENTEST_USE_BOOST_UUID)
    list(APPEND _gentest_boost_components uuid)
    list(APPEND _gentest_boost_links Boost::uuid)
    target_compile_definitions(gentest_runtime PUBLIC GENTEST_USE_BOOST_UUID)
endif()
if(_gentest_boost_components)
    find_package(Boost CONFIG REQUIRED COMPONENTS ${_gentest_boost_components})
    target_link_libraries(gentest_runtime PUBLIC ${_gentest_boost_links})
endif()

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

# Publish headers for dependents
target_sources(${PROJECT_NAME}
    INTERFACE
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
find_package(fmt CONFIG REQUIRED)
